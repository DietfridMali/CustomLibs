#define NOMINMAX

#include "rendertarget.h"
#include "vkcontext.h"
#include "image_layout_tracker.h"

#include <algorithm>
#include <cstdio>

// =================================================================================================
// Vulkan RenderTarget implementation — Phase B (resource setup)
//
// What is functional in this Phase B pass:
//   • BufferInfo (Init / SetState / AllocRTV / Release) ported to VkImage + VkImageView +
//     VmaAllocation + ImageLayoutTracker.
//   • RenderTarget::Init / Destroy / Create / AllocRTVs / FreeRTVs.
//   • CreateBuffer / CreateColorBuffer / CreateDepthBuffer / CreateSRV (= vkCreateImageView).
//   • CreateRenderArea (BaseQuad setup, API-neutral).
//   • BufferHandle (logical id accessor, API-neutral).
//
// What is deferred to Phase C (needs VkCommandBuffer / CommandList port):
//   • Activate / Enable / Disable / Deactivate (vkCmdBeginRendering / vkCmdEndRendering).
//   • Render / RenderAsTexture / AutoRender (descriptor-set bind pipeline).
//   • Clear / Fill (vkCmdClearColorImage or LoadOp=CLEAR via VkRenderingAttachmentInfo).
//   • AttachBuffer / DetachBuffer / BindBuffer (image-layout transitions on a live cmd buffer).
//   • SetViewport / DepthBufferIsActive / SelectDrawBuffers / EnableBuffers / UpdateTransformation.
//
// The DX12 1:1 carry-over for the deferred methods is preserved in the #if 0 block below
// as reference and will be ported when the CL/RT lifecycle moves to VkCommandBuffer.

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


bool BufferInfo::AllocRTV(void)
{
    // In DX12 this allocated a CPU descriptor handle for the render-target view. In Vulkan
    // the image view created in RenderTarget::CreateColorBuffer (or CreateDepthBuffer) already
    // serves as the attachment view; nothing extra to allocate. Returns whether the view exists.
    return m_imageView != VK_NULL_HANDLE;
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
    // Initial layout transition (UNDEFINED -> SHADER_READ_ONLY) deferred until first
    // Activate — needs a live VkCommandBuffer, which Phase C provides.
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

    // Phase C: m_cmdList = commandListHandler.CreateCmdList(...); m_cmdList->Open();
    m_cmdList = nullptr;

    int attachmentIndex = 0;
    for (int i = 0; i < m_colorBufferCount; ++i)
        CreateBuffer(i, attachmentIndex, BufferInfo::btColor);
    m_haveRTVs = true;

    m_vertexBufferCount = params.vertexBufferCount;
    m_extraBufferIndex = CreateSpecialBuffers(BufferInfo::btVertex, attachmentIndex, params.vertexBufferCount);
    m_depthBufferIndex = CreateSpecialBuffers(BufferInfo::btDepth, attachmentIndex, params.depthBufferCount);
    m_stencilBufferIndex = CreateSpecialBuffers(BufferInfo::btStencil, attachmentIndex, params.stencilBufferCount);

    // Phase C: m_cmdList->Flush(); m_cmdList = nullptr;

    int w = width * scale;
    int h = height * scale;
    m_viewport = Viewport(0, 0, w, h);
    CreateRenderArea();
    m_isAvailable = true;
    return true;
}


void RenderTarget::Destroy(void)
{
    // Phase C: if (m_cmdList) m_cmdList->Close();
    m_cmdList = nullptr;

    for (int i = 0; i < m_bufferCount; ++i)
        m_bufferInfo[i].Release();
    m_isAvailable = false;
    m_haveRTVs = false;
    m_bufferCount = m_colorBufferCount = m_vertexBufferCount = 0;
    m_depthBufferIndex = m_stencilBufferIndex = m_extraBufferIndex = -1;
    m_bufferInfo.Reset();
}


bool RenderTarget::AllocRTVs(void)
{
    // In Vulkan the image views created in CreateColorBuffer already serve as RTV equivalents.
    // Mark as available; AllocRTV per buffer is a no-op that just confirms the view exists.
    if (m_haveRTVs)
        return true;
    for (int i = 0; i < m_colorBufferCount; ++i)
        if (not m_bufferInfo[i].AllocRTV())
            return false;
    return m_haveRTVs = true;
}


void RenderTarget::FreeRTVs(void)
{
    // Free attachment image views (Vulkan equivalent of releasing the DX12 RTV descriptor slot).
    // The underlying VkImage and VmaAllocation remain — only the view is destroyed.
    VkDevice device = vkContext.Device();
    for (int i = 0; i < m_colorBufferCount; ++i) {
        BufferInfo& info = m_bufferInfo[i];
        if ((info.m_imageView != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE)) {
            vkDestroyImageView(device, info.m_imageView, nullptr);
            info.m_imageView = VK_NULL_HANDLE;
        }
    }
    m_haveRTVs = false;
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
// Phase C: rendering / activation / clearing / binding methods.
// The DX12 1:1 carry-over below is preserved as reference and will be ported when the CL/RT
// lifecycle moves to VkCommandBuffer + vkCmdBeginRendering / vkCmdEndRendering.

#if 0

#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "commandlist.h"
#include "dx12context.h"
#include "gfxstates.h"
#include "sampler_cache.h"

// (DX12 original bodies for Activate / Enable / Disable / Render / Clear / Fill / AttachBuffer /
// DetachBuffer / BindBuffer / SelectDrawBuffers / EnableBuffers / Deactivate / SetViewport /
// DepthBufferIsActive / GetAsTexture / GetDepthAsTexture / GetDepthAsShadowTexture /
// UpdateTransformation / RenderAsTexture / AutoRender are intentionally NOT carried into the
// Vulkan tree by-line. They will be reimplemented from scratch in Phase C on top of the
// dynamic-rendering API. The corresponding DX12 sources remain in
// rendertools/directx/src/rendertarget.cpp as the porting reference.)

#endif

// =================================================================================================
