#define NOMINMAX

#include "rendertarget.h"
#include "vkcontext.h"
#include "image_layout_tracker.h"
#include "vkupload.h"	// CreateReadbackBuffer / one-shot command buffer for ReadBuffer ()
#include "resource_handler.h"

#include <algorithm>
#include <cstdio>

// =================================================================================================
// Vulkan RenderTarget implementation - Phase B (resource setup)
//
// What is functional in this Phase B pass:
//   - BufferInfo (Init / SetState / Release) ported to VkImage + VkImageView +
//     VmaAllocation + ImageLayoutTracker.
//   - RenderTarget::Init / Destroy / Create.
//   - CreateBuffer / CreateColorBuffer / CreateDepthBuffer / CreateSRV (= vkCreateImageView).
//   - CreateRenderArea (BaseQuad setup, API-neutral).
//   - BufferHandle (logical id accessor, API-neutral).
//
// Rendering / activation / clearing / binding methods (Activate / Enable / Disable / Deactivate /
// Render / RenderAsTexture / AutoRender / Fill / Clear / ClearColorBuffers / ClearDepthBuffer /
// ClearStencilBuffer / AttachBuffer / DetachBuffer / BindBuffer / SetViewport /
// DepthBufferIsActive / SelectDrawBuffers / EnableBuffers / UpdateTransformation) are below,
// implemented on top of the CommandList port (vkCmdBeginRendering / vkCmdEndRendering /
// VkRenderingAttachmentInfo).

static constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
static constexpr VkFormat kVertexFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
// HDR sky-map format (TSP). RGBA16F = 8 Byte/Pixel, supports HDR cumulus + alpha for premultiplied
// composit. Sampled+Storage usage so compute can imageStore() and graphics can sampler-read.
static constexpr VkFormat kSkyMapFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
// D32_SFLOAT: 4 Byte/Pixel, single-channel, kein Stencil. ShadowMap braucht keinen Stencil und
// kein Treiber-Padding (D24S8 wird auf NVIDIA als D32+S8-Plane = 5 Byte/Pixel allokiert).
static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
// Depth + stencil for targets that request a stencil plane (stencilBufferCount > 0). Stencil is never a
// buffer of its own -- the hardware interleaves both planes, and VK_FORMAT_S8_UINT is an optional format
// hardly any driver exposes. D32_SFLOAT_S8_UINT keeps the depth precision of the plain kDepthFormat, at
// the price of the padding the comment above avoids; only stencil targets pay it.
static constexpr VkFormat kDepthStencilFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

// -------------------------------------------------------------------------------------------------

static VkFormat FormatForType(BufferInfo::eBufferType type)
{
    switch (type) {
    case BufferInfo::btDepth:
    case BufferInfo::btStencil:
        return kDepthFormat;
    case BufferInfo::btVertex:
        return kVertexFormat;
    case BufferInfo::btSkyMap:
        return kSkyMapFormat;
    default:
        return kColorFormat;
    }
}


static VkImageUsageFlags UsageForType(BufferInfo::eBufferType type)
{
    if ((type == BufferInfo::btDepth) or (type == BufferInfo::btStencil))
        return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
         | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
         | VK_IMAGE_USAGE_STORAGE_BIT;
}


static VkImageAspectFlags AspectForType(BufferInfo::eBufferType type)
{
    // btStencil is never created as a buffer of its own (see RenderTarget::m_stencilBufferIndex), so only
    // btDepth arrives here. A depth buffer WITH a stencil plane does not go through this helper either --
    // CreateDepthBuffer picks its aspect from m_hasStencil, because the type alone cannot tell.
    if ((type == BufferInfo::btDepth) or (type == BufferInfo::btStencil))
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}


static bool CreateRTImage(int w, int h, VkFormat format, VkImageUsageFlags usage,
                          VkImage& outImage, VmaAllocation& outAllocation,
                          int arrayLayers = 1, bool cubeCompatible = false) noexcept
{
    VmaAllocator allocator = vkContext.Allocator();
    if (allocator == VK_NULL_HANDLE)
        return false;

    VkImageCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = uint32_t(w);
    info.extent.height = uint32_t(h);
    info.extent.depth = 1;
    info.mipLevels = 1;
    // Six for a cube map, one for everything else. CUBE_COMPATIBLE is what allows a
    // VK_IMAGE_VIEW_TYPE_CUBE view over those six layers later on.
    info.arrayLayers = uint32_t(arrayLayers);
    if (cubeCompatible)
        info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult res = vmaCreateImage(allocator, &info, &allocInfo, &outImage, &outAllocation, nullptr);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "RenderTarget::CreateRTImage: vmaCreateImage failed (%d)\n", (int)res);
        return false;
    }
    return true;
}

// =================================================================================================
// BufferInfo

void BufferInfo::Init(void)
{
    m_image = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_imageView = VK_NULL_HANDLE;
    m_depthSampleView = VK_NULL_HANDLE;
    for (int face = 0; face < 6; ++face)
        m_cubeView[face] = VK_NULL_HANDLE;
    m_layoutTracker = ImageLayoutTracker { };
    m_srvIndex = UINT32_MAX;
    m_type = btColor;
}


void BufferInfo::SetState(VkCommandBuffer cb, eBufferType usageHint, bool asShaderRead)
{
    if (m_image == VK_NULL_HANDLE)
        return;
    if (cb == VK_NULL_HANDLE)
        return;

    if (asShaderRead) {
        if ((usageHint == btDepth) or (usageHint == btStencil))
            m_layoutTracker.ToShadowInput(cb);
        else
            m_layoutTracker.ToShaderInput(cb);
        return;
    }
    if ((usageHint == btDepth) or (usageHint == btStencil))
        m_layoutTracker.ToDepthAttachment(cb);
    else
        m_layoutTracker.ToColorAttachment(cb);
}


void BufferInfo::Release(void)
{
    VkDevice device = vkContext.Device();
    VmaAllocator allocator = vkContext.Allocator();

    // Defer GPU-resource teardown by one frame slot - in-flight command buffers may still
    // reference the image/view. Same pattern as Texture::Destroy(m_isDisposable). Safe in the
    // app-shutdown path as long as gfxResourceHandler.Cleanup() processes both frame slots
    // before the handler itself is torn down.
    if ((m_imageView != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE)) {
        VkImageView view = m_imageView;
        gfxResourceHandler.TrackCleanup([device, view]() {
            vkDestroyImageView(device, view, nullptr);
        });
        m_imageView = VK_NULL_HANDLE;
    }
    // The six per face views of a cube map buffer. m_imageView above is the cube view over all layers
    // and is a separate object, so both have to go.
    for (int face = 0; face < 6; ++face) {
        if ((m_cubeView[face] == VK_NULL_HANDLE) or (device == VK_NULL_HANDLE))
            continue;

        VkImageView view = m_cubeView[face];

        gfxResourceHandler.TrackCleanup([device, view]() {
            vkDestroyImageView(device, view, nullptr);
        });
        m_cubeView[face] = VK_NULL_HANDLE;
    }
    if ((m_depthSampleView != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE)) {
        VkImageView view = m_depthSampleView;
        gfxResourceHandler.TrackCleanup([device, view]() {
            vkDestroyImageView(device, view, nullptr);
        });
        m_depthSampleView = VK_NULL_HANDLE;
    }
    if ((m_image != VK_NULL_HANDLE) and (allocator != VK_NULL_HANDLE)) {
        VkImage image = m_image;
        VmaAllocation alloc = m_allocation;
        gfxResourceHandler.TrackCleanup([allocator, image, alloc]() {
            vmaDestroyImage(allocator, image, alloc);
        });
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
    Init();
}

// =================================================================================================
// RenderTarget setup

RenderTarget::RenderTarget()
{
    Init();
}


void RenderTarget::Init(void)
{
    m_width = m_height = 0;
    m_scale = 1;
    m_bufferCount = m_colorBufferCount = m_vertexBufferCount = 0;
    m_extraBufferIndex = -1;
    m_depthBufferIndex = -1;
    m_stencilBufferIndex = -1;
    m_hasStencil = false;
    m_computeBufferIndex = -1;
    m_computeBufferCount = 0;
    m_activeBufferIndex = 0;
    m_lastDestination = -1;
    m_pingPong = false;
    m_isAvailable = false;
    m_drawBufferGroup = dbAll;
    m_depthMode = dbmWrite;
    m_clearColor = ColorData::Invisible;
    m_bufferInfo.Reset();
    m_customDrawBuffers.Reset();
}


bool RenderTarget::CreateSRV(BufferInfo& info, VkFormat viewFormat, VkImageAspectFlags aspect)
{
    VkDevice device = vkContext.Device();
    if ((device == VK_NULL_HANDLE) or (info.m_image == VK_NULL_HANDLE))
        return false;

    VkImageViewCreateInfo vci { };
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = info.m_image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = viewFormat;
    vci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.subresourceRange.aspectMask = aspect;
    vci.subresourceRange.baseMipLevel = 0;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount = 1;

    VkResult res = vkCreateImageView(device, &vci, nullptr, &info.m_imageView);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "RenderTarget::CreateSRV: vkCreateImageView failed (%d)\n", (int)res);
        return false;
    }
    info.m_srvIndex = uint32_t(uintptr_t(info.m_imageView) & 0xFFFFFFFFu);  // logical id (low 32 bits of handle)
    return true;
}


void RenderTarget::CreateDepthBuffer(BufferInfo& info, int w, int h)
{
    VkFormat fmt = m_hasStencil ? kDepthStencilFormat : kDepthFormat;
    // The attachment side addresses both planes; the sampling view below must name exactly one aspect,
    // because Vulkan forbids sampling a view that spans depth AND stencil.
    VkImageAspectFlags attachmentAspect = m_hasStencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                                                       : VK_IMAGE_ASPECT_DEPTH_BIT;
    if (not CreateRTImage(w, h, fmt, UsageForType(info.m_type),
                          info.m_image, info.m_allocation))
        return;
    info.m_layoutTracker.Init(info.m_image, VK_IMAGE_LAYOUT_UNDEFINED, attachmentAspect);

    // Attachment view: depth (plus stencil, if the target asked for a stencil plane).
    if (not CreateSRV(info, fmt, attachmentAspect))
        return;

    // Sampling view: depth aspect only - for use as a sampled texture (sampler2DShadow / shadow map).
    VkDevice device = vkContext.Device();
    VkImageViewCreateInfo vci { };
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = info.m_image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &vci, nullptr, &info.m_depthSampleView) != VK_SUCCESS)
        fprintf(stderr, "RenderTarget::CreateDepthBuffer: depth-sample view creation failed\n");
}


void RenderTarget::CreateColorBuffer(BufferInfo& info, int w, int h)
{
    VkFormat fmt = (info.m_type == BufferInfo::btColor) ? info.m_colorFormat : FormatForType(info.m_type);
    if (not CreateRTImage(w, h, fmt, UsageForType(info.m_type),
                          info.m_image, info.m_allocation))
        return;
    info.m_layoutTracker.Init(info.m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

    if (not CreateSRV(info, fmt, VK_IMAGE_ASPECT_COLOR_BIT))
        return;
    // Initial layout transition (UNDEFINED -> SHADER_READ_ONLY / COLOR_ATTACHMENT) is performed
    // on the first Activate via BufferInfo::SetState on the live VkCommandBuffer.
}


// One image with six layers: six single layer views to render into, one cube view to sample with.
//
// An attachment addresses exactly one layer, so rendering the six faces needs six views; sampling by
// direction needs a VK_IMAGE_VIEW_TYPE_CUBE view over all six. Both come off the same image.

void RenderTarget::CreateCubemapBuffer(BufferInfo& info, int edge)
{
    VkDevice device = vkContext.Device();

    info.m_colorFormat = m_cubeMapFormat;
    // Square by definition, so the edge length serves for both dimensions.
    if (not CreateRTImage(edge, edge, m_cubeMapFormat, UsageForType(BufferInfo::btColor),
                          info.m_image, info.m_allocation, 6, true))
        return;
    info.m_layoutTracker.Init(info.m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

    for (int face = 0; face < 6; ++face) {
        VkImageViewCreateInfo vci { };

        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = info.m_image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = m_cubeMapFormat;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = uint32_t(face);
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, nullptr, &info.m_cubeView[face]) != VK_SUCCESS) {
            fprintf(stderr, "RenderTarget::CreateCubemapBuffer: vkCreateImageView failed for face %d\n", face);
            return;
        }
    }

    VkImageViewCreateInfo cubeView { };

    cubeView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cubeView.image = info.m_image;
    cubeView.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cubeView.format = m_cubeMapFormat;
    cubeView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cubeView.subresourceRange.levelCount = 1;
    cubeView.subresourceRange.baseArrayLayer = 0;
    cubeView.subresourceRange.layerCount = 6;
    if (vkCreateImageView(device, &cubeView, nullptr, &info.m_imageView) != VK_SUCCESS)
        fprintf(stderr, "RenderTarget::CreateCubemapBuffer: cube view creation failed\n");
}


// One face of a cube map buffer as the current attachment. The target has to be active; this only
// swaps which of the six layer views is attached, so everything downstream is unaffected. Six of these
// with a draw in between capture the surroundings of a point.

bool RenderTarget::SelectCubeFace(int face, int bufferIndex)
{
    if ((m_cubeMapCount <= 0) or (face < 0) or (face > 5))
        return false;

    int index = (bufferIndex < 0) ? m_cubeMapIndex : bufferIndex;

    if ((index < 0) or (index >= m_bufferCount) or (m_bufferInfo[index].m_type != BufferInfo::btCubemap))
        return false;
    if (m_bufferInfo[index].m_cubeView[face] == VK_NULL_HANDLE)
        return false;
    m_cubeFace = face;
    return true;
}


void RenderTarget::CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType)
{
    BufferInfo& info = m_bufferInfo[bufferIndex];
    info.Init();
    info.m_type = bufferType;
    info.m_colorFormat = m_colorFormat;

    int w = m_width * m_scale;
    int h = m_height * m_scale;

    if ((bufferType == BufferInfo::btDepth) or (bufferType == BufferInfo::btStencil))
        CreateDepthBuffer(info, w, h);
    else if (bufferType == BufferInfo::btCubemap)
        // Edge length is the WIDTH - a cube map is square, and taking the height as well would quietly
        // produce something that is not a cube.
        CreateCubemapBuffer(info, w);
    else
        CreateColorBuffer(info, w, h);
    ++m_bufferCount;
    (void)attachmentIndex;
}


int RenderTarget::CreateSpecialBuffers(BufferInfo::eBufferType bufferType, int& attachmentIndex, int bufferCount)
{
    if (not bufferCount)
        return -1;
    for (int i = 0; i < bufferCount; ++i)
        CreateBuffer(m_bufferCount, attachmentIndex, bufferType);
    return m_bufferCount - bufferCount;
}


bool RenderTarget::Create(int width, int height, int scale, const RTCreationParams& params)
{
    Destroy();

    if (vkContext.Device() == VK_NULL_HANDLE)
        return false;

    m_name = params.name;
    m_width = width;
    m_height = height;
    m_scale = scale;
    m_colorBufferCount = std::min(params.colorBufferCount, RT_MAX_COLOR_BUFFERS);
    m_colorFormat = params.colorFormat;
    m_cubeMapFormat = params.cubeMapFormat;
    // Stencil is a plane of the depth buffer, not a buffer of its own (see m_stencilBufferIndex). Asking
    // for stencil without depth still yields one combined buffer.
    m_hasStencil = params.stencilBufferCount > 0;
    int depthBufferCount = m_hasStencil ? std::max(params.depthBufferCount, 1) : params.depthBufferCount;
    m_bufferInfo.Resize(params.skyMapCount + params.colorBufferCount + params.vertexBufferCount + depthBufferCount + params.cubeMapCount);
    // One sampling wrapper per colour buffer, dimensioned here and never again - see m_renderTextures.
    m_renderTextures.Resize(m_colorBufferCount);
    for (int i = 0; i < m_renderTextures.Length(); i++)
        m_renderTextures[i].m_filtering = m_filtering;
    // Compute ping-pong (>=2 compute buffers) qualifies for the pingPong flag as well.
    m_pingPong = (m_colorBufferCount > 1) or (params.skyMapCount > 1);
    m_isScreenBuffer = params.isScreenBuffer;
    m_cmdList = nullptr;  // attached lazily on first Activate via commandListHandler.CreateCmdList

    int attachmentIndex = 0;

    // Color buffers first, using m_bufferCount as the next free slot.
    for (int i = 0; i < m_colorBufferCount; ++i)
        CreateBuffer(m_bufferCount, attachmentIndex, BufferInfo::btColor);

    m_vertexBufferCount = params.vertexBufferCount;
    m_extraBufferIndex = CreateSpecialBuffers(BufferInfo::btVertex, attachmentIndex, params.vertexBufferCount);
    m_depthBufferIndex = CreateSpecialBuffers(BufferInfo::btDepth, attachmentIndex, depthBufferCount);
    m_stencilBufferIndex = m_hasStencil ? m_depthBufferIndex : -1;

    // Compute buffers come last so the existing color/vertex/depth-buffer iterations
    // (e.g. SelectDrawBuffers, which assumes m_bufferInfo[0..m_colorBufferCount-1] are color
    // buffers) remain valid. Caller addresses them via m_computeBufferIndex + slot.
    m_computeBufferIndex = (params.skyMapCount > 0)
        ? CreateSpecialBuffers(BufferInfo::btSkyMap, attachmentIndex, params.skyMapCount)
        : -1;
    m_computeBufferCount = params.skyMapCount;
    // Cube maps last, for the same reason as the compute buffers: everything that walks the colour
    // buffers assumes they sit contiguously from index 0.
    m_cubeMapIndex = (params.cubeMapCount > 0)
        ? CreateSpecialBuffers(BufferInfo::btCubemap, attachmentIndex, params.cubeMapCount)
        : -1;
    m_cubeMapCount = params.cubeMapCount;
    m_cubeFace = 0;

    int w = width * scale;
    int h = height * scale;
    m_viewport = Viewport(0, 0, w, h);
    CreateRenderArea();
    m_isAvailable = true;
    return true;
}


void RenderTarget::Destroy(void)
{
    if (m_cmdList)
        m_cmdList->Close();
    m_cmdList = nullptr;

    for (int i = 0; i < m_bufferCount; ++i)
        m_bufferInfo[i].Release();
    m_isAvailable = false;
    m_bufferCount = m_colorBufferCount = m_vertexBufferCount = 0;
    m_depthBufferIndex = m_stencilBufferIndex = m_extraBufferIndex = -1;
    m_hasStencil = false;
    m_computeBufferIndex = -1;
    m_computeBufferCount = 0;
    m_bufferInfo.Reset();
}


void RenderTarget::CreateRenderArea(void)
{
    m_viewportArea.Setup(BaseQuadMesh::defaultVertices[BaseQuadMesh::voCenter], BaseQuadMesh::defaultTexCoords[BaseQuadMesh::tcRegular]);
}


uint32_t& RenderTarget::BufferHandle(int bufferIndex)
{
    if ((bufferIndex >= 0) and (bufferIndex < m_colorBufferCount))
        return m_bufferInfo[bufferIndex].m_srvIndex;
    static uint32_t invalid = UINT32_MAX;
    return invalid;
}


// =================================================================================================
// Phase-C rendering / activation / clearing / binding methods.
// 1:1 port of the DX12 RenderTarget bodies (rendertools/directx/src/rendertarget.cpp), with
// all DX12 API calls replaced by their Vulkan dynamic-rendering equivalents:
//   D3D12_RESOURCE_STATE_*  -> ImageLayoutTracker.TransitionTo via BufferInfo::SetState
//   OMSetRenderTargets      -> vkCmdBeginRendering / vkCmdEndRendering
//   ClearRenderTargetView   -> VkRenderingAttachmentInfo loadOp = CLEAR + clearValue
//   ClearDepthStencilView   -> VkRenderingAttachmentInfo loadOp = CLEAR + clearValue (depth)
//   vkCmdClearAttachments   -> mid-pass clears (Fill / Clear / ClearStencilBuffer)
//   SetGraphicsRootDescriptorTable -> Texture::Bind on m_renderTexture (bind table tracking)

#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "commandlist.h"
#include "vkcontext.h"
#include "gfxstates.h"
#include "sampler_cache.h"
#include "descriptor_pool_handler.h"
#include "shader.h"
#include "pipeline_cache.h"

namespace {

VkClearValue MakeClearColor(const RGBAColor& c) {
    VkClearValue v{};
    const float* d = c.Data();
    v.color.float32[0] = d[0];
    v.color.float32[1] = d[1];
    v.color.float32[2] = d[2];
    v.color.float32[3] = d[3];
    return v;
}

VkClearValue MakeClearDepth(float depth) {
    VkClearValue v{};
    v.depthStencil.depth = depth;
    v.depthStencil.stencil = 0;
    return v;
}

}  // anonymous

// -------------------------------------------------------------------------------------------------
// BeginRendering / EndRendering - manage the vkCmdBeginRendering scope for this RT.

void RenderTarget::BeginRendering(bool clearColor, bool clearDepth)
{
    if (m_isInRendering)
        EndRendering();
    if (not m_cmdList or not m_cmdList->IsRecording())
        return;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE)
        return;

    VkRenderingAttachmentInfo colors[RT_MAX_COLOR_BUFFERS]{};
    int colorCount = 0;
    BufferInfo* depthInfo = ActiveDepthInfo();   // own depth, or a shared source's depth (SetDepthSource)
    bool wantDepth = (depthInfo != nullptr);

    auto ConfigColor = [&](int i) {
        BufferInfo& bi = m_bufferInfo[i];
        if (bi.m_imageView == VK_NULL_HANDLE)
            return;
        VkRenderingAttachmentInfo a{};
        a.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        a.imageView   = bi.m_imageView;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp      = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        a.clearValue  = MakeClearColor(m_clearColor);
        colors[colorCount++] = a;
    };

    if (m_drawBufferGroup == dbDepth) {
        // depth-only: no colour writes
    }
    else if (m_drawBufferGroup == dbSingle) {
        if ((m_activeBufferIndex >= 0) and (m_activeBufferIndex < m_colorBufferCount))
            ConfigColor(m_activeBufferIndex);
    }
    else if (m_drawBufferGroup == dbExtra) {
        for (int j = 0, i = VertexBufferIndex(); j < m_vertexBufferCount; ++j, ++i)
            ConfigColor(i);
    }
    else if (m_drawBufferGroup == dbAll) {
        for (int i = 0; i < m_colorBufferCount; ++i)
            ConfigColor(i);
        for (int j = 0, i = VertexBufferIndex(); j < m_vertexBufferCount; ++j, ++i)
            ConfigColor(i);
    }
    else if (m_drawBufferGroup == dbCustom) {
        // Slot order is the caller's; an unused slot keeps its position with a null image view (Vulkan
        // discards writes to it), or every later fragment output would shift down by one slot.
        int listed = m_customDrawBuffers.Length();
        for (int i = 0; (i < listed) and (colorCount < RT_MAX_COLOR_BUFFERS); ++i) {
            int bufferIndex = m_customDrawBuffers[i];
            VkImageView view = VK_NULL_HANDLE;
            if ((bufferIndex >= 0) and (bufferIndex < m_bufferCount))
                view = m_bufferInfo[bufferIndex].m_imageView;
            VkRenderingAttachmentInfo a{};
            a.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            a.imageView   = view;
            a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            a.loadOp      = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            a.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
            a.clearValue  = MakeClearColor(m_clearColor);
            colors[colorCount++] = a;
        }
    }
    else {  // dbColor - color only
        for (int i = 0; i < m_colorBufferCount; ++i)
            ConfigColor(i);
    }

    VkRenderingAttachmentInfo depth{};
    if (wantDepth) {
        depth.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView   = depthInfo->m_imageView;
        // Read-only depth: a shared source (SetDepthSource, only ever tested against) or an activation
        // that asked for dbmReadOnly. The read-only layout lets the SAME image serve as the depth
        // attachment AND a sampled texture in the same pass (WBOIT soft particles). An own depth buffer
        // in dbmWrite stays writable.
        bool readOnlyDepth = (m_depthSource != nullptr) or (m_depthMode == dbmReadOnly);
        depth.imageLayout = readOnlyDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                          : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        // force LOAD even when the activation clears, so the existing depth survives the pass.
        depth.loadOp      = readOnlyDepth ? VK_ATTACHMENT_LOAD_OP_LOAD
                                          : (clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD);
        depth.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue  = MakeClearDepth(1.0f);
    }

    VkRenderingInfo info{};
    info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea.offset    = { 0, 0 };
    info.renderArea.extent    = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)) };
    info.layerCount           = 1;
    info.colorAttachmentCount = uint32_t(colorCount);
    info.pColorAttachments    = (colorCount > 0) ? colors : nullptr;
    info.pDepthAttachment     = wantDepth ? &depth : nullptr;
    // With a stencil plane the same view serves the stencil slot. It must be named here as well, or the
    // pipeline (which declares stencilAttachmentFormat, see PipelineCache) does not match the render pass
    // and the stencil test never runs. Contents are always preserved -- shadow volumes clear the stencil
    // themselves, between passes.
    VkRenderingAttachmentInfo stencil{};
    if (wantDepth and (DepthFormat() == kDepthStencilFormat)) {
        stencil            = depth;
        stencil.loadOp     = VK_ATTACHMENT_LOAD_OP_LOAD;
        stencil.storeOp    = VK_ATTACHMENT_STORE_OP_STORE;
        info.pStencilAttachment = &stencil;
    }

    vkCmdBeginRendering(cb, &info);
    m_isInRendering = true;
}


void RenderTarget::EndRendering(void)
{
    if (not m_isInRendering)
        return;
    if (m_cmdList and m_cmdList->IsRecording()) {
        VkCommandBuffer cb = m_cmdList->GfxList();
        if (cb != VK_NULL_HANDLE)
            vkCmdEndRendering(cb);
    }
    m_isInRendering = false;
}

// -------------------------------------------------------------------------------------------------
// AttachBuffer / DetachBuffer

bool RenderTarget::AttachBuffer(int bufferIndex)
{
    if ((bufferIndex < 0) or (bufferIndex >= m_bufferCount) or not m_cmdList)
        return false;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE)
        return false;
    BufferInfo::eBufferType usage = m_bufferInfo[bufferIndex].m_type;
    m_bufferInfo[bufferIndex].SetState(cb, usage, false);
    return true;
}


bool RenderTarget::DetachBuffer(int bufferIndex)
{
    if ((bufferIndex < 0) or (bufferIndex >= m_bufferCount) or not m_cmdList)
        return false;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE)
        return false;
    BufferInfo::eBufferType usage = m_bufferInfo[bufferIndex].m_type;
    m_bufferInfo[bufferIndex].SetState(cb, usage, true);
    return true;
}

// -------------------------------------------------------------------------------------------------
// SelectDrawBuffers / DepthBufferIsActive / EnableBuffers / Enable / Activate

bool RenderTarget::SelectDrawBuffers(const RTActivationParams& params)
{
    if (not m_cmdList)
        return false;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE)
        return false;

    // Validate dbSingle's target up front, before we touch the render-pass scope below.
    if ((params.drawBufferGroup == dbSingle) and
        ((params.bufferIndex < 0) or (params.bufferIndex >= m_bufferInfo.Length())))
        return false;

    SetDepthMode(params.depthMode);

    // AttachBuffer/DetachBuffer issue image-layout barriers, which Vulkan forbids inside an active
    // vkCmdBeginRendering scope. When this is called mid-pass -- a post-effect switching the scene
    // buffer to colour-0-only and back (the wet-splat composite, the single-output overlays) -- close
    // the pass first, reconfigure the attachments, then reopen it preserving the contents
    // (loadOp = LOAD). In the Enable() path m_isInRendering is already false (Enable ended it and
    // re-Begins itself), so this self-management is a no-op there and never double-begins.
    bool wasRendering = m_isInRendering;
    if (wasRendering)
        EndRendering();

    if (params.drawBufferGroup == dbDepth) {
        for (int i = 0; i < m_colorBufferCount; ++i)
            DetachBuffer(i);
    }
    else if (params.drawBufferGroup == dbSingle) {
        m_drawBufferGroup = dbSingle;
        m_activeBufferIndex = params.bufferIndex;
        AttachBuffer(params.bufferIndex);
        for (int i = 0; i < m_colorBufferCount; ++i)
            if (i != params.bufferIndex)
                DetachBuffer(i);
        // Also detach the vertex-buffer MRTs (worldNormals/worldPos) -> SHADER_READ_ONLY: a post-effect
        // rendering only into colour 0 must not keep them bound as attachments, and one that samples
        // them (wetSplats) needs them in a readable layout.
        for (int j = 0, i = VertexBufferIndex(); j < m_vertexBufferCount; ++j, ++i)
            DetachBuffer(i);
    }
    else {
        m_activeBufferIndex = -1;
        m_drawBufferGroup = (params.drawBufferGroup == dbNone) ? dbAll : params.drawBufferGroup;
        if (m_drawBufferGroup == dbAll) {
            for (int i = 0; i < m_bufferCount; ++i) {
                if (m_bufferInfo[i].m_type == BufferInfo::btDepth or m_bufferInfo[i].m_type == BufferInfo::btStencil)
                    continue;
                AttachBuffer(i);
            }
        }
        else if (m_drawBufferGroup == dbColor) {
            for (int i = 0; i < m_colorBufferCount; ++i)
                AttachBuffer(i);
            for (int i = m_colorBufferCount; i < m_bufferCount; ++i)
                DetachBuffer(i);
        }
        else if (m_drawBufferGroup == dbExtra) {
            for (int i = 0; i < m_colorBufferCount; ++i)
                DetachBuffer(i);
            for (int j = 0, i = VertexBufferIndex(); j < m_vertexBufferCount; ++j, ++i)
                AttachBuffer(i);
        }
        else if (m_drawBufferGroup == dbCustom) {
            // Caller-defined setup: slot i draws into m_customDrawBuffers[i]. Everything not named is
            // detached (-> SHADER_READ_ONLY), so a buffer left out of the list can be sampled by the pass.
            int listed = m_customDrawBuffers.Length();
            for (int i = 0; i < m_bufferCount; ++i) {
                BufferInfo::eBufferType type = m_bufferInfo[i].m_type;
                if ((type != BufferInfo::btColor) and (type != BufferInfo::btVertex))
                    continue;
                bool isTarget = false;
                for (int j = 0; (j < listed) and not isTarget; ++j)
                    isTarget = (m_customDrawBuffers[j] == i);
                if (isTarget)
                    AttachBuffer(i);
                else
                    DetachBuffer(i);
            }
        }
    }
    if (HaveDepthBuffer(true)) {
        // dbmReadOnly: keep the own depth image in the read-only depth layout instead of the writable
        // attachment layout, so this pass can test against it AND sample it (soft particles / WBOIT).
        // The barrier must run here, outside the vkCmdBeginRendering scope.
        if (m_depthMode == dbmReadOnly)
            m_bufferInfo[m_depthBufferIndex].m_layoutTracker.ToDepthReadOnly(cb);
        else
            AttachBuffer(m_depthBufferIndex);
    }
    else if ((m_depthSource != nullptr) and (m_depthSource->m_depthBufferIndex >= 0))
        // Shared depth (SetDepthSource): transition the foreign depth image into the read-only depth layout
        // (DEPTH_STENCIL_READ_ONLY_OPTIMAL via asShaderRead) on OUR command buffer so it can be both tested
        // against AND sampled in this pass (WBOIT soft particles). The barrier must run here, outside the
        // vkCmdBeginRendering scope; a later in-pass GetDepthAsTexture (ToShadowInput) is then a no-op
        // (TransitionTo early-outs on the same layout), so no forbidden in-pass barrier is emitted.
        // ToDepthReadOnly rather than ToShadowInput: the image stays BOUND as the depth attachment here,
        // so the destination scopes have to cover the depth test too, not just the shader fetch.
        m_depthSource->m_bufferInfo[m_depthSource->m_depthBufferIndex].m_layoutTracker.ToDepthReadOnly(cb);

    // Reopen the pass we closed above, with the reconfigured attachment set and contents preserved.
    if (wasRendering)
        BeginRendering(false, false);
    return true;
}


void RenderTarget::SelectCustomDrawBuffers(const CustomDrawBufferList& bufferIndices)
{
    m_customDrawBuffers = bufferIndices;
    m_activeBufferIndex = -1;
    m_drawBufferGroup = dbCustom;
}


bool RenderTarget::DepthBufferIsActive(int bufferIndex, eDrawBufferGroups /*drawBufferGroup*/)
{
    // a shared depth source (SetDepthSource) is treated like an own depth buffer
    if ((m_depthBufferIndex < 0) and (m_depthSource == nullptr))
        return false;
    if (bufferIndex >= 0)
        return (m_bufferInfo[bufferIndex].m_type == BufferInfo::btColor) or (m_bufferInfo[bufferIndex].m_type == BufferInfo::btDepth);
    return (m_drawBufferGroup == dbAll) or (m_drawBufferGroup == dbColor) or (m_drawBufferGroup == dbDepth) or (m_drawBufferGroup == dbCustom);
}


bool RenderTarget::EnableBuffers(const RTActivationParams& params)
{
    if (not SelectDrawBuffers(params))
        return false;
    gfxStates.SetDepthTest(DepthBufferIsActive(params.bufferIndex, params.drawBufferGroup));
    return true;
}


bool RenderTarget::Enable(const RTActivationParams& params)
{
    if (not m_isAvailable)
        return false;
    m_activeBufferIndex = (params.bufferIndex < 0) ? 0 : (params.bufferIndex % m_bufferCount);
    m_drawBufferGroup = params.drawBufferGroup;

    if (m_cmdList == nullptr) {
        m_cmdList = commandListHandler.CreateCmdList(String("RenderTarget:") + m_name);
        if (not m_cmdList or not m_cmdList->Open(not params.reactivate))
            return false;
    }
    // Layout transitions in EnableBuffers must happen outside any active vkCmdBeginRendering
    // scope (Vulkan forbids vkCmdPipelineBarrier2 with image-memory barriers inside a render
    // pass instance). On re-activate of an already-active RT (e.g. ping-pong in
    // TextEffects::AntiAlias) m_isInRendering is still true from the previous BeginRendering -
    // close it first, run the transitions, then re-open with the new attachment layout.
    if (m_isInRendering)
        EndRendering();

    if (not EnableBuffers(params))
        return false;
    BeginRendering(params.clear, params.clear);
    return true;
}


// The filter the colour buffer is read back with. Nothing else about a render target's sampling is
// negotiable - one level, no wrapping, no depth compare - but whether it is scaled or read texel for
// texel is the owner's business, not RenderTargetTexture::SetParams ()'s.

void RenderTarget::SetFiltering(GfxFilterMode filtering) {
    if (filtering == m_filtering)
        return;
    m_filtering = filtering;
    // The filtering belongs to the target, so every buffer's wrapper takes it.
    for (int i = 0; i < m_renderTextures.Length(); i++) {
        m_renderTextures[i].m_filtering = filtering;
        m_renderTextures[i].SetParams(true);
    }
}

// =================================================================================================

bool RenderTarget::IsActive(void) noexcept
{
    return baseRenderer.IsActiveDrawBuffer(this);
}


bool RenderTarget::Activate(const RTActivationParams& params)
{
    if (/*m_wasActivated or*/ params.reactivate)
        baseRenderer.RenderStates() = m_renderStates;
    else if (not m_wasActivated)
        baseRenderer.PushViewport();
    baseRenderer.ActivateDrawBuffer(this);
    if (not Enable(params)) {
        baseRenderer.DeactivateDrawBuffer(this);
        return false;
    }
    // Activate/Deactivate are a balanced viewport push/pop pair: Activate pushes the caller's
    // viewport, Deactivate's PopViewport restores it. A reactivation (via DeactivateDrawBuffer)
    // has no Deactivate of its own, so it must not push or set a viewport - the caller's
    // viewport is restored by the PopViewport immediately following in Deactivate().
    SetViewport(true);
    m_wasActivated = true;
    return true;
}


void RenderTarget::Disable(bool deactivate) noexcept
{
    if (not IsEnabled())
        return;
    m_renderStates = baseRenderer.RenderStates();
    EndRendering();
    VkCommandBuffer cb = m_cmdList ? m_cmdList->GfxList() : VK_NULL_HANDLE;
    if (cb != VK_NULL_HANDLE) {
        for (int i = 0; i < m_colorBufferCount; ++i)
            m_bufferInfo[i].SetState(cb, BufferInfo::btColor, true);
        for (int j = 0, i = VertexBufferIndex(); j < m_vertexBufferCount; ++j, ++i)
            m_bufferInfo[i].SetState(cb, BufferInfo::btVertex, true);
        if (m_depthBufferIndex >= 0)
            m_bufferInfo[m_depthBufferIndex].SetState(cb, BufferInfo::btDepth, true);
    }
    m_cmdList->Close(deactivate);
    m_cmdList = nullptr;
}


void RenderTarget::Deactivate(void) noexcept
{
    baseRenderer.DeactivateDrawBuffer(this);
    baseRenderer.PopViewport();
    m_wasActivated = false;
}

// -------------------------------------------------------------------------------------------------
// Viewport / Fill / Clear*

void RenderTarget::SetViewport(bool flipVertically) noexcept
{
    baseRenderer.SetViewport(m_viewport, GetWidth(true), GetHeight(true), flipVertically);
}


void RenderTarget::Fill(RGBAColor color)
{
    if (not m_cmdList or not m_isInRendering)
        return;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE or m_colorBufferCount == 0)
        return;

    AutoArray<VkClearAttachment> attachments(m_colorBufferCount);
    int n = 0;
    VkClearValue clearVal = MakeClearColor(color);
    for (int i = 0; i < m_colorBufferCount; ++i) {
        VkClearAttachment a{};
        a.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
        a.colorAttachment = uint32_t(i);
        a.clearValue      = clearVal;
        attachments[n++] = a;
    }
    VkClearRect rect{};
    rect.rect.offset = { 0, 0 };
    rect.rect.extent = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)) };
    rect.layerCount  = 1;
    vkCmdClearAttachments(cb, uint32_t(n), attachments.Data(), 1, &rect);
}


void RenderTarget::Clear(const RTActivationParams& params)
{
    // Vulkan: BeginRendering already folds the initial clear via LoadOp_CLEAR. This entry exists
    // for mid-pass re-clears via vkCmdClearAttachments inside the active render scope.
    if (not params.clear or not m_cmdList or not m_isInRendering)
        return;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE)
        return;

    int maxAtts = (m_customDrawBuffers.Length() > m_colorBufferCount) ? m_customDrawBuffers.Length() : m_colorBufferCount;
    AutoArray<VkClearAttachment> atts(maxAtts + 1);
    int n = 0;
    VkClearValue cv = MakeClearColor(m_clearColor);
    if (params.bufferIndex < 0) {
        // Clear by ATTACHMENT SLOT. With a custom setup the slots are the caller's list (and an unused
        // slot has no image view, so it is skipped); otherwise slot i is colour buffer i.
        if (m_drawBufferGroup == dbCustom) {
            for (int i = 0; (i < m_customDrawBuffers.Length()) and (i < RT_MAX_COLOR_BUFFERS); ++i) {
                int bufferIndex = m_customDrawBuffers[i];
                if ((bufferIndex < 0) or (bufferIndex >= m_bufferCount))
                    continue;
                VkClearAttachment a{};
                a.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
                a.colorAttachment = uint32_t(i);
                a.clearValue      = cv;
                atts[n++] = a;
            }
        }
        else {
            for (int i = 0; i < m_colorBufferCount; ++i) {
                VkClearAttachment a{};
                a.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
                a.colorAttachment = uint32_t(i);
                a.clearValue      = cv;
                atts[n++] = a;
            }
        }
    }
    else if (params.bufferIndex < m_colorBufferCount) {
        VkClearAttachment a{};
        a.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
        a.colorAttachment = uint32_t(params.bufferIndex);
        a.clearValue      = cv;
        atts[n++] = a;
    }
    // A read-only depth activation must not clear the depth it is only allowed to test against (same rule
    // as in the DX backend, where the writable DSV is not even bound).
    if (HaveDepthBuffer(true) and (params.depthMode != dbmReadOnly)) {
        VkClearAttachment a{};
        a.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        a.clearValue = MakeClearDepth(1.0f);
        atts[n++] = a;
    }
    if (n == 0)
        return;
    VkClearRect rect{};
    rect.rect.offset = { 0, 0 };
    rect.rect.extent = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)) };
    rect.layerCount  = 1;
    vkCmdClearAttachments(cb, uint32_t(n), atts.Data(), 1, &rect);
}


// WBOIT per-buffer clear: clear one colour attachment to an explicit value mid-pass (accum and revealage
// need different clears, which the single m_clearColor can't express). Attachment index = draw-buffer slot.
void RenderTarget::ClearColorBuffer(int bufferIndex, RGBAColor color)
{
    if (not m_cmdList or not m_isInRendering)
        return;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if ((cb == VK_NULL_HANDLE) or (bufferIndex < 0) or (bufferIndex >= m_colorBufferCount))
        return;
    VkClearAttachment a{};
    a.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
    a.colorAttachment = uint32_t(bufferIndex);
    a.clearValue      = MakeClearColor(color);
    VkClearRect rect{};
    rect.rect.offset = { 0, 0 };
    rect.rect.extent = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)) };
    rect.layerCount  = 1;
    vkCmdClearAttachments(cb, 1, &a, 1, &rect);
}


void RenderTarget::ClearColorBuffers(void)
{
    if (not m_cmdList or not m_isInRendering)
        return;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE or m_colorBufferCount == 0)
        return;
    AutoArray<VkClearAttachment> atts(m_colorBufferCount);
    int n = 0;
    VkClearValue cv = MakeClearColor(m_clearColor);
    for (int i = 0; i < m_colorBufferCount; ++i) {
        VkClearAttachment a{};
        a.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
        a.colorAttachment = uint32_t(i);
        a.clearValue      = cv;
        atts[n++] = a;
    }
    VkClearRect rect{};
    rect.rect.offset = { 0, 0 };
    rect.rect.extent = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)) };
    rect.layerCount  = 1;
    vkCmdClearAttachments(cb, uint32_t(n), atts.Data(), 1, &rect);
}


void RenderTarget::ClearDepthBuffer(float clearValue)
{
    if (not HaveDepthBuffer(true) or not m_cmdList or not m_isInRendering)
        return;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE)
        return;
    VkClearAttachment a{};
    a.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    a.clearValue = MakeClearDepth(clearValue);
    VkClearRect rect{};
    rect.rect.offset = { 0, 0 };
    rect.rect.extent = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)) };
    rect.layerCount  = 1;
    vkCmdClearAttachments(cb, 1, &a, 1, &rect);
}


void RenderTarget::ClearStencilBuffer(void)
{
    // Gated on an own stencil PLANE, not just on a depth buffer: without one the clear would address an
    // aspect the attachment does not have.
    if (not HaveStencilBuffer(true) or not m_cmdList or not m_isInRendering)
        return;
    VkCommandBuffer cb = m_cmdList->GfxList();
    if (cb == VK_NULL_HANDLE)
        return;
    VkClearAttachment a{};
    a.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    a.clearValue.depthStencil.stencil = 0;
    VkClearRect rect{};
    rect.rect.offset = { 0, 0 };
    rect.rect.extent = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)) };
    rect.layerCount  = 1;
    vkCmdClearAttachments(cb, 1, &a, 1, &rect);
}

// -------------------------------------------------------------------------------------------------
// BindBuffer - bind RT color buffer as shader sampling source.

bool RenderTarget::BindBuffer(int bufferIndex, int tmuIndex)
{
    if ((bufferIndex < 0) or (bufferIndex >= m_bufferInfo.Length()))
        return false;
    if (tmuIndex < 0)
        tmuIndex = bufferIndex;
    BufferInfo& info = m_bufferInfo[bufferIndex];
    if (info.m_imageView == VK_NULL_HANDLE)
        return false;
    // Transition only on our own CL and only when no render-pass scope is open on it.
    // Foreign-CL barriers or barriers inside vkCmdBeginRendering are forbidden; in the
    // pingpong path the next Activate's DetachBuffer will issue the transition outside
    // the pass, and on a disabled RT Disable has already transitioned all buffers.
    if (m_cmdList and not m_isInRendering) {
        VkCommandBuffer cb = m_cmdList->GfxList();
        if (cb != VK_NULL_HANDLE)
            info.m_layoutTracker.ToShaderInput(cb);
    }
    RenderTargetTexture* texture = GetRenderTexture(bufferIndex);
    if (texture == nullptr)
        texture = &m_externalTexture;
    if (not texture->m_hasParams)
        texture->SetParams(false);
    texture->m_image = info.m_image;
    texture->m_imageView = info.m_imageView;
    texture->m_handle = info.m_srvIndex;
    texture->Validate();
    return texture->Bind(tmuIndex);
}

// -------------------------------------------------------------------------------------------------
// GetAsTexture / GetDepthAsTexture / GetDepthAsShadowTexture

// GetAsTexture* hand a RenderTarget's color / depth buffer over to the shader-input side
// of the pipeline. The buffer was last written as a color or depth attachment, so the image
// is in COLOR_ATTACHMENT_OPTIMAL / DEPTH_STENCIL_ATTACHMENT_OPTIMAL. Vulkan requires a layout
// transition to SHADER_READ_ONLY_OPTIMAL before the image can be sampled - emit it here on
// the active CommandBuffer so the caller (Texture::Bind + vkCmdDraw) sees a sample-ready
// image. ToShaderInput is a no-op when the tracker already records SHADER_READ_ONLY_OPTIMAL.

Texture* RenderTarget::GetAsTexture(const RTRenderParams& params, int /*tmuIndex*/)
{
    int bufferIndex = params.source % m_bufferCount;
    BufferInfo& info = m_bufferInfo[bufferIndex];
    if (info.m_image == VK_NULL_HANDLE)
        return nullptr;
    // Transition only on our own CL and only when no render-pass scope is open on it.
    // Foreign-CL barriers or barriers inside vkCmdBeginRendering are forbidden; in the
    // pingpong path the next Activate's DetachBuffer will issue the transition outside
    // the pass, and on a disabled RT Disable has already transitioned all buffers.
    if (m_cmdList and not m_isInRendering) {
        VkCommandBuffer cb = m_cmdList->GfxList();
        if (cb != VK_NULL_HANDLE)
            info.m_layoutTracker.ToShaderInput(cb);
    }
    RenderTargetTexture* texture = GetRenderTexture(bufferIndex);
    if (texture == nullptr)
        texture = &m_externalTexture;
    texture->m_image = info.m_image;
    texture->m_imageView = info.m_imageView;
    texture->m_handle = info.m_srvIndex;
    texture->Validate();
    return texture;
}


Texture* RenderTarget::GetDepthAsTexture(void)
{
    if (m_depthBufferIndex < 0)
        return nullptr;
    BufferInfo& info = m_bufferInfo[m_depthBufferIndex];
    if (info.m_image == VK_NULL_HANDLE)
        return nullptr;
    // Transition only on our own CL and only when no render-pass scope is open on it.
    // Foreign-CL barriers or barriers inside vkCmdBeginRendering are forbidden; in the
    // pingpong path the next Activate's DetachBuffer will issue the transition outside
    // the pass, and on a disabled RT Disable has already transitioned all buffers.
    if (m_cmdList and not m_isInRendering) {
        VkCommandBuffer cb = m_cmdList->GfxList();
        if (cb != VK_NULL_HANDLE)
            info.m_layoutTracker.ToShadowInput(cb);
    }
    m_depthTexture.m_image = info.m_image;
    m_depthTexture.m_imageView = (info.m_depthSampleView != VK_NULL_HANDLE) ? info.m_depthSampleView : info.m_imageView;
    m_depthTexture.m_sampleLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    m_depthTexture.m_handle = info.m_srvIndex;
    m_depthTexture.Validate();
    return &m_depthTexture;
}


Texture* RenderTarget::GetDepthAsShadowTexture(void)
{
    if (m_depthBufferIndex < 0)
        return nullptr;
    BufferInfo& info = m_bufferInfo[m_depthBufferIndex];
    if (info.m_image == VK_NULL_HANDLE)
        return nullptr;
    // Transition only on our own CL and only when no render-pass scope is open on it.
    // Foreign-CL barriers or barriers inside vkCmdBeginRendering are forbidden; in the
    // pingpong path the next Activate's DetachBuffer will issue the transition outside
    // the pass, and on a disabled RT Disable has already transitioned all buffers.
    if (m_cmdList and not m_isInRendering) {
        VkCommandBuffer cb = m_cmdList->GfxList();
        if (cb != VK_NULL_HANDLE)
            info.m_layoutTracker.ToShadowInput(cb);
    }
    m_shadowTexture.m_image = info.m_image;
    m_shadowTexture.m_imageView = (info.m_depthSampleView != VK_NULL_HANDLE) ? info.m_depthSampleView : info.m_imageView;
    m_shadowTexture.m_sampleLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    m_shadowTexture.m_handle = info.m_srvIndex;
    m_shadowTexture.Validate();
    return &m_shadowTexture;
}

// -------------------------------------------------------------------------------------------------
// UpdateTransformation / RenderAsTexture / Render / AutoRender - API-neutral, 1:1 from DX12.

bool RenderTarget::UpdateTransformation(const RTRenderParams& params)
{
    bool haveTransformation = false;
    if (params.centerOrigin) {
        haveTransformation = true;
        baseRenderer.Translate(0.5, 0.5, 0);
    }
    if (params.rotation) {
        haveTransformation = true;
        baseRenderer.Rotate(params.rotation, 0, 0, 1);
    }
    else if (params.scale != 1.0f) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, params.scale, 1);
    }
    return haveTransformation;
}


bool RenderTarget::RenderAsTexture(Texture* source, const RTRenderParams& params, const RGBAColor& color)
{
    bool deactivate = false;
    if (params.destination >= 0) {
        deactivate = not IsActive();
        if (not Activate({ .bufferIndex = params.destination, .drawBufferGroup = RenderTarget::dbSingle, .clear = true, .reactivate = not deactivate }))
            return false;
        m_lastDestination = params.destination;
    }
    baseRenderer.PushMatrix();
    bool applyTransformation = UpdateTransformation(params);
    if (params.shader) {
        if (applyTransformation)
            params.shader->UpdateMatrices();
        m_viewportArea.Render(params.shader, source);
    }
    else {
        if (params.premultiply)
            m_viewportArea.Premultiply();
        baseRenderer.Set2DRenderStates(params.destination < 0);
        m_viewportArea.Render(nullptr, source, color);
    }
    baseRenderer.PopMatrix();
    if (deactivate)
        Deactivate();
    return true;
}


bool RenderTarget::Render(const RTRenderParams& params, const RGBAColor& color)
{
    if (params.destination >= 0)
        m_lastDestination = params.destination;
    return RenderAsTexture((params.source == params.destination) ? nullptr : GetAsTexture(params), params, color);
}


bool RenderTarget::AutoRender(const RTRenderParams& params, const RGBAColor& color)
{
    return Render({ .source = m_lastDestination, .destination = NextBuffer(m_lastDestination), .clearBuffer = params.clearBuffer, .scale = params.scale, .shader = params.shader }, color);
}


void RenderTarget::FillPipelineKey(PipelineKey& key) noexcept
{
    key.colorFormatCount = 0;
    for (auto& f : key.colorFormats)
        f = VK_FORMAT_UNDEFINED;
    key.depthFormat = VK_FORMAT_UNDEFINED;

    switch (m_drawBufferGroup) {
        case dbDepth:
            break;
        case dbSingle:
            if ((m_activeBufferIndex >= 0) and (m_activeBufferIndex < m_colorBufferCount))
                key.colorFormats[key.colorFormatCount++] = m_colorFormat;
            break;
        case dbExtra:
            for (int j = 0; j < m_vertexBufferCount; ++j)
                key.colorFormats[key.colorFormatCount++] = kVertexFormat;
            break;
        case dbAll:
            for (int i = 0; i < m_colorBufferCount; ++i)
                key.colorFormats[key.colorFormatCount++] = m_colorFormat;
            for (int j = 0; j < m_vertexBufferCount; ++j)
                key.colorFormats[key.colorFormatCount++] = kVertexFormat;
            break;
        case dbCustom:
            // The pipeline has to name the attachment formats in the SAME slot order BeginRendering binds
            // them, or the render pass and the pipeline do not match. An unused slot is UNDEFINED.
            for (int i = 0; (i < m_customDrawBuffers.Length()) and (key.colorFormatCount < RT_MAX_COLOR_BUFFERS); ++i) {
                int bufferIndex = m_customDrawBuffers[i];
                key.colorFormats[key.colorFormatCount++] =
                    ((bufferIndex >= 0) and (bufferIndex < m_bufferCount))
                    ? ((m_bufferInfo[bufferIndex].m_type == BufferInfo::btVertex) ? kVertexFormat : m_colorFormat)
                    : VK_FORMAT_UNDEFINED;
            }
            break;
        default: // dbColor - color only
            for (int i = 0; i < m_colorBufferCount; ++i)
                key.colorFormats[key.colorFormatCount++] = m_colorFormat;
            break;
    }
    if (HaveActiveDepthBuffer())
        key.depthFormat = DepthFormat();
}

// =================================================================================================
// Reading a colour buffer back to the CPU.
//
// Like D3D12, Vulkan cannot map a device-local image: the texels go through a host visible buffer
// first, filled by a GPU copy. The copy is recorded into a one-shot command buffer that is submitted
// and waited on right here - which is why this must not be called while a frame is being recorded.
//
// vkCmdCopyImageToBuffer writes TIGHTLY PACKED rows (bufferRowLength = 0), so unlike the D3D12 path
// there is no row padding to undo and the destination can be filled with one memcpy.

static size_t ColorFormatBytes(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;
        case VK_FORMAT_R16_SFLOAT:
            return 2;
        case VK_FORMAT_R32_SFLOAT:
            return 4;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
            return 4;
        default:
            return 0;
    }
}


size_t RenderTarget::BufferSize(int bufferIndex) {
    if ((bufferIndex < 0) or (bufferIndex >= m_colorBufferCount))
        return 0;

    size_t texelBytes = ColorFormatBytes(m_colorFormat);

    return texelBytes ? size_t(GetWidth(true)) * size_t(GetHeight(true)) * texelBytes : 0;
}


bool RenderTarget::ReadBuffer(int bufferIndex, void* buffer, size_t bufferSize) {
    if (not (buffer and m_isAvailable))
        return false;
    if ((bufferIndex < 0) or (bufferIndex >= m_colorBufferCount))
        return false;

    BufferInfo& info = m_bufferInfo[bufferIndex];

    if (info.m_image == VK_NULL_HANDLE)
        return false;

    size_t needed = BufferSize(bufferIndex);

    if ((needed == 0) or (bufferSize < needed))
        return false;

    VkStagingBuffer readback;

    if (not CreateReadbackBuffer(VkDeviceSize(needed), readback))
        return false;

    OneShotCommandBuffer cmd;

    if (not BeginSingleTimeCommands(cmd)) {
        readback.Destroy();
        return false;
    }

    VkImageLayout layoutBefore = info.m_layoutTracker.Layout();

    info.m_layoutTracker.ToTransferSrc(cmd.cb);

    VkBufferImageCopy copy { };

    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;       // tightly packed, like the upload path
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = { 0, 0, 0 };
    copy.imageExtent = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)), 1 };

    vkCmdCopyImageToBuffer(cmd.cb, info.m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buffer, 1, &copy);
    // Back to the layout the caller left it in - the next pass expects to find it there.
    if (layoutBefore != VK_IMAGE_LAYOUT_UNDEFINED)
        info.m_layoutTracker.TransitionTo(cmd.cb, layoutBefore);
    if (not EndSingleTimeCommands(cmd)) {
        readback.Destroy();
        return false;
    }
    if (readback.mapped == nullptr) {
        readback.Destroy();
        return false;
    }
    // Host visible and coherent through VMA's AUTO mapping, so what the copy wrote is visible here.
    memcpy(buffer, readback.mapped, needed);
    readback.Destroy();
    return true;
}

// =================================================================================================

bool RenderTarget::WriteBuffer(int bufferIndex, const void* data, size_t dataSize) {
    if (not (data and m_isAvailable))
        return false;
    if ((bufferIndex < 0) or (bufferIndex >= m_colorBufferCount))
        return false;

    BufferInfo& info = m_bufferInfo[bufferIndex];

    if (info.m_image == VK_NULL_HANDLE)
        return false;

    size_t needed = BufferSize(bufferIndex);

    if ((needed == 0) or (dataSize < needed))
        return false;

    VkStagingBuffer staging;

    if (not CreateStagingBuffer(VkDeviceSize(needed), staging))
        return false;
    if (staging.mapped == nullptr) {
        staging.Destroy();
        return false;
    }
    memcpy(staging.mapped, data, needed);

    OneShotCommandBuffer cmd;

    if (not BeginSingleTimeCommands(cmd)) {
        staging.Destroy();
        return false;
    }

    VkImageLayout layoutBefore = info.m_layoutTracker.Layout();

    info.m_layoutTracker.ToTransferDst(cmd.cb);

    VkBufferImageCopy copy { };

    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;       // tightly packed
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = { 0, 0, 0 };
    copy.imageExtent = { uint32_t(GetWidth(true)), uint32_t(GetHeight(true)), 1 };

    vkCmdCopyBufferToImage(cmd.cb, staging.buffer, info.m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    if (layoutBefore != VK_IMAGE_LAYOUT_UNDEFINED)
        info.m_layoutTracker.TransitionTo(cmd.cb, layoutBefore);

    bool ok = EndSingleTimeCommands(cmd);

    staging.Destroy();
    return ok;
}

// =================================================================================================