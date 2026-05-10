#define NOMINMAX

#include "rendertarget.h"
#include "vkcontext.h"
#include "image_layout_tracker.h"
#include "resource_handler.h"

#include <algorithm>
#include <cstdio>

// =================================================================================================
// Vulkan RenderTarget implementation — Phase B (resource setup)
//
// What is functional in this Phase B pass:
//   • BufferInfo (Init / SetState / Release) ported to VkImage + VkImageView +
//     VmaAllocation + ImageLayoutTracker.
//   • RenderTarget::Init / Destroy / Create.
//   • CreateBuffer / CreateColorBuffer / CreateDepthBuffer / CreateSRV (= vkCreateImageView).
//   • CreateRenderArea (BaseQuad setup, API-neutral).
//   • BufferHandle (logical id accessor, API-neutral).
//
// Rendering / activation / clearing / binding methods (Activate / Enable / Disable / Deactivate /
// Render / RenderAsTexture / AutoRender / Fill / Clear / ClearColorBuffers / ClearDepthBuffer /
// ClearStencilBuffer / AttachBuffer / DetachBuffer / BindBuffer / SetViewport /
// DepthBufferIsActive / SelectDrawBuffers / EnableBuffers / UpdateTransformation) are below,
// implemented on top of the CommandList port (vkCmdBeginRendering / vkCmdEndRendering /
// VkRenderingAttachmentInfo).

static constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
static constexpr VkFormat kVertexFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
static constexpr VkFormat kDepthFormat = VK_FORMAT_D24_UNORM_S8_UINT;

// -------------------------------------------------------------------------------------------------

static VkFormat FormatForType(BufferInfo::eBufferType type)
{
    switch (type) {
    case BufferInfo::btDepth:
    case BufferInfo::btStencil:
        return kDepthFormat;
    case BufferInfo::btVertex:
        return kVertexFormat;
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
    if (type == BufferInfo::btDepth)
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    if (type == BufferInfo::btStencil)
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}


static bool CreateRTImage(int w, int h, VkFormat format, VkImageUsageFlags usage,
                          VkImage& outImage, VmaAllocation& outAllocation) noexcept
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
    info.arrayLayers = 1;
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
        m_layoutTracker.ToShaderRead(cb);
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
    if ((m_imageView != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE)) {
        vkDestroyImageView(device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if ((m_depthSampleView != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE)) {
        vkDestroyImageView(device, m_depthSampleView, nullptr);
        m_depthSampleView = VK_NULL_HANDLE;
    }
    if ((m_image != VK_NULL_HANDLE) and (allocator != VK_NULL_HANDLE)) {
        vmaDestroyImage(allocator, m_image, m_allocation);
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
    m_activeBufferIndex = 0;
    m_lastDestination = -1;
    m_pingPong = false;
    m_isAvailable = false;
    m_drawBufferGroup = dbAll;
    m_clearColor = ColorData::Invisible;
    m_bufferInfo.Reset();
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
    if (not CreateRTImage(w, h, kDepthFormat, UsageForType(info.m_type),
                          info.m_image, info.m_allocation))
        return;
    info.m_layoutTracker.Init(info.m_image, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

    // Attachment view: depth + stencil aspect (used as DSV in Dynamic Rendering).
    if (not CreateSRV(info, kDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        return;

    // Sampling view: depth aspect only — for use as a sampled texture (sampler2DShadow / shadow map).
    VkDevice device = vkContext.Device();
    VkImageViewCreateInfo vci { };
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = info.m_image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kDepthFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &vci, nullptr, &info.m_depthSampleView) != VK_SUCCESS)
        fprintf(stderr, "RenderTarget::CreateDepthBuffer: depth-sample view creation failed\n");
}


void RenderTarget::CreateColorBuffer(BufferInfo& info, int w, int h)
{
    VkFormat fmt = FormatForType(info.m_type);
    if (not CreateRTImage(w, h, fmt, UsageForType(info.m_type),
                          info.m_image, info.m_allocation))
        return;
    info.m_layoutTracker.Init(info.m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

    if (not CreateSRV(info, fmt, VK_IMAGE_ASPECT_COLOR_BIT))
        return;
    // Initial layout transition (UNDEFINED -> SHADER_READ_ONLY / COLOR_ATTACHMENT) is performed
    // on the first Activate via BufferInfo::SetState on the live VkCommandBuffer.
}


void RenderTarget::CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType)
{
    BufferInfo& info = m_bufferInfo[bufferIndex];
    info.Init();
    info.m_type = bufferType;

    int w = m_width * m_scale;
    int h = m_height * m_scale;

    if ((bufferType == BufferInfo::btDepth) or (bufferType == BufferInfo::btStencil))
        CreateDepthBuffer(info, w, h);
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
    m_bufferInfo.Resize(params.colorBufferCount + params.vertexBufferCount + params.depthBufferCount + params.stencilBufferCount);
    m_pingPong = m_colorBufferCount > 1;
    m_isScreenBuffer = params.isScreenBuffer;
    m_cmdList = nullptr;  // attached lazily on first Activate via commandListHandler.CreateCmdList

    int attachmentIndex = 0;
    for (int i = 0; i < m_colorBufferCount; ++i)
        CreateBuffer(i, attachmentIndex, BufferInfo::btColor);

    m_vertexBufferCount = params.vertexBufferCount;
    m_extraBufferIndex = CreateSpecialBuffers(BufferInfo::btVertex, attachmentIndex, params.vertexBufferCount);
    m_depthBufferIndex = CreateSpecialBuffers(BufferInfo::btDepth, attachmentIndex, params.depthBufferCount);
    m_stencilBufferIndex = CreateSpecialBuffers(BufferInfo::btStencil, attachmentIndex, params.stencilBufferCount);

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
    m_bufferInfo.Reset();
}


void RenderTarget::CreateRenderArea(void)
{
    m_viewportArea.Setup(BaseQuad::defaultVertices[BaseQuad::voCenter], BaseQuad::defaultTexCoords[BaseQuad::tcRegular]);
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
// BeginRendering / EndRendering — manage the vkCmdBeginRendering scope for this RT.

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
    bool wantDepth = HaveDepthBuffer(true);

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
        for (int j = 0, i = VertexBufferIndex(); j < m_vertexBufferCount and colorCount < RT_MAX_COLOR_BUFFERS; ++j, ++i)
            ConfigColor(i);
    }
    else {
        for (int i = 0; i < m_colorBufferCount and colorCount < RT_MAX_COLOR_BUFFERS; ++i)
            ConfigColor(i);
    }

    VkRenderingAttachmentInfo depth{};
    if (wantDepth) {
        BufferInfo& di = m_bufferInfo[m_depthBufferIndex];
        depth.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView   = di.m_imageView;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp      = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
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

    if (params.drawBufferGroup == dbDepth) {
        for (int i = 0; i < m_colorBufferCount; ++i)
            m_bufferInfo[i].SetState(cb, BufferInfo::btColor, true);
    }
    else if (params.drawBufferGroup == dbSingle) {
        m_drawBufferGroup = dbSingle;
        if ((params.bufferIndex < 0) or (params.bufferIndex >= m_bufferInfo.Length()))
            return false;
        m_activeBufferIndex = params.bufferIndex;
        m_bufferInfo[params.bufferIndex].SetState(cb, BufferInfo::btColor, false);
        for (int i = 0; i < m_colorBufferCount; ++i)
            if (i != params.bufferIndex)
                m_bufferInfo[i].SetState(cb, BufferInfo::btColor, true);
    }
    else {
        m_activeBufferIndex = -1;
        m_drawBufferGroup = (params.drawBufferGroup == dbNone) ? dbAll : params.drawBufferGroup;
        if (m_drawBufferGroup == dbAll) {
            for (int i = 0; i < m_bufferCount; ++i) {
                if (m_bufferInfo[i].m_type == BufferInfo::btDepth or m_bufferInfo[i].m_type == BufferInfo::btStencil)
                    continue;
                m_bufferInfo[i].SetState(cb, BufferInfo::btColor, false);
            }
        }
        else if (m_drawBufferGroup == dbColor) {
            for (int i = 0; i < m_colorBufferCount; ++i)
                m_bufferInfo[i].SetState(cb, BufferInfo::btColor, false);
            for (int i = m_colorBufferCount; i < m_bufferCount; ++i)
                m_bufferInfo[i].SetState(cb, BufferInfo::btColor, true);
        }
        else if (m_drawBufferGroup == dbExtra) {
            for (int i = 0; i < m_colorBufferCount; ++i)
                m_bufferInfo[i].SetState(cb, BufferInfo::btColor, true);
            for (int j = 0, i = VertexBufferIndex(); j < m_vertexBufferCount; ++j, ++i)
                m_bufferInfo[i].SetState(cb, BufferInfo::btVertex, false);
        }
    }
    if (HaveDepthBuffer(true))
        m_bufferInfo[m_depthBufferIndex].SetState(cb, BufferInfo::btDepth, false);
    return true;
}


bool RenderTarget::DepthBufferIsActive(int bufferIndex, eDrawBufferGroups /*drawBufferGroup*/)
{
    if (m_depthBufferIndex < 0)
        return false;
    if (bufferIndex >= 0)
        return (m_bufferInfo[bufferIndex].m_type == BufferInfo::btColor) or (m_bufferInfo[bufferIndex].m_type == BufferInfo::btDepth);
    return (m_drawBufferGroup == dbAll) or (m_drawBufferGroup == dbColor) or (m_drawBufferGroup == dbDepth);
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
    m_flushOnDisable = params.flush;
    SetViewport();

    if (not EnableBuffers(params))
        return false;
    BeginRendering(params.clear, params.clear);
    return true;
}


bool RenderTarget::Activate(const RTActivationParams& params)
{
    baseRenderer.ActivateDrawBuffer(this);
    if (not Enable(params)) {
        baseRenderer.DeactivateDrawBuffer(this);
        return false;
    }
    baseRenderer.PushViewport();
    SetViewport();
    if (params.reactivate)
        baseRenderer.RenderStates() = m_renderStates;
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
    }
    if (m_flushOnDisable)
        m_cmdList->Flush();
    else
        m_cmdList->Close(deactivate);
    m_cmdList = nullptr;
}


void RenderTarget::Deactivate(void) noexcept
{
    baseRenderer.DeactivateDrawBuffer(this);
    baseRenderer.PopViewport();
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

    AutoArray<VkClearAttachment> atts(m_colorBufferCount + 1);
    int n = 0;
    VkClearValue cv = MakeClearColor(m_clearColor);
    if (params.bufferIndex < 0) {
        for (int i = 0; i < m_colorBufferCount; ++i) {
            VkClearAttachment a{};
            a.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
            a.colorAttachment = uint32_t(i);
            a.clearValue      = cv;
            atts[n++] = a;
        }
    }
    else if (params.bufferIndex < m_colorBufferCount) {
        VkClearAttachment a{};
        a.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
        a.colorAttachment = uint32_t(params.bufferIndex);
        a.clearValue      = cv;
        atts[n++] = a;
    }
    if (HaveDepthBuffer(true)) {
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
    if (not HaveDepthBuffer(true) or not m_cmdList or not m_isInRendering)
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
// BindBuffer — bind RT color buffer as shader sampling source.

bool RenderTarget::BindBuffer(int bufferIndex, int tmuIndex)
{
    if ((bufferIndex < 0) or (bufferIndex >= m_bufferInfo.Length()))
        return false;
    if (tmuIndex < 0)
        tmuIndex = bufferIndex;
    BufferInfo& info = m_bufferInfo[bufferIndex];
    if (info.m_imageView == VK_NULL_HANDLE)
        return false;
    if (not m_renderTexture.m_hasParams)
        m_renderTexture.SetParams(false);
    m_renderTexture.m_image = info.m_image;
    m_renderTexture.m_imageView = info.m_imageView;
    m_renderTexture.m_handle = info.m_srvIndex;
    m_renderTexture.Validate();
    return m_renderTexture.Bind(tmuIndex);
}

// -------------------------------------------------------------------------------------------------
// GetAsTexture / GetDepthAsTexture / GetDepthAsShadowTexture

Texture* RenderTarget::GetAsTexture(const RTRenderParams& params, int /*tmuIndex*/)
{
    BufferInfo& info = m_bufferInfo[params.source % m_bufferCount];
    if (info.m_image == VK_NULL_HANDLE)
        return nullptr;
    m_renderTexture.m_image = info.m_image;
    m_renderTexture.m_imageView = info.m_imageView;
    m_renderTexture.m_handle = info.m_srvIndex;
    m_renderTexture.Validate();
    return &m_renderTexture;
}


Texture* RenderTarget::GetDepthAsTexture(void)
{
    if (m_depthBufferIndex < 0)
        return nullptr;
    BufferInfo& info = m_bufferInfo[m_depthBufferIndex];
    if (info.m_image == VK_NULL_HANDLE)
        return nullptr;
    m_depthTexture.m_image = info.m_image;
    m_depthTexture.m_imageView = (info.m_depthSampleView != VK_NULL_HANDLE) ? info.m_depthSampleView : info.m_imageView;
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
    m_shadowTexture.m_image = info.m_image;
    m_shadowTexture.m_imageView = (info.m_depthSampleView != VK_NULL_HANDLE) ? info.m_depthSampleView : info.m_imageView;
    m_shadowTexture.m_handle = info.m_srvIndex;
    m_shadowTexture.Validate();
    return &m_shadowTexture;
}

// -------------------------------------------------------------------------------------------------
// UpdateTransformation / RenderAsTexture / Render / AutoRender — API-neutral, 1:1 from DX12.

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
    if (params.destination >= 0) {
        if (not Activate({ .bufferIndex = params.destination, .drawBufferGroup = RenderTarget::dbSingle, .clear = true, .flush = true }))
            return false;
        m_lastDestination = params.destination;
        gfxStates.SetBlending(0);
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
    return Render(params, color);
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
                key.colorFormats[key.colorFormatCount++] = kColorFormat;
            break;
        case dbExtra:
            for (int j = 0; j < m_vertexBufferCount and key.colorFormatCount < 8; ++j)
                key.colorFormats[key.colorFormatCount++] = kVertexFormat;
            break;
        default: // dbAll, dbColor, dbCustom
            for (int i = 0; i < m_colorBufferCount and key.colorFormatCount < 8; ++i)
                key.colorFormats[key.colorFormatCount++] = kColorFormat;
            break;
    }
    if (HaveDepthBuffer(true))
        key.depthFormat = kDepthFormat;
}

// =================================================================================================