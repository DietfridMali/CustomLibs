#include "gfxstates.h"
#include "vkframework.h"
#include "vkcontext.h"
#include "commandlist.h"
#include "gfxrenderer.h"
#include "base_displayhandler.h"

#include <cstdio>

// =================================================================================================
// TextureSlotInfo — API-neutral (slot bookkeeping only, no GPU calls).

TextureSlotInfo::TextureSlotInfo(GLenum typeTag)
    : m_typeTag(typeTag)
{
    m_srvIndices.fill(0u);
}


int TextureSlotInfo::Find(uint32_t srvIndex) const noexcept {
    if (not srvIndex)
        return -1;
    for (int i = 0; i < m_maxUsed; ++i)
        if (m_srvIndices[i] == srvIndex)
            return i;
    return -1;
}


int TextureSlotInfo::Bind(uint32_t srvIndex, int slotIndex) noexcept {
    if (slotIndex < 0) {
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (not m_srvIndices[i]) { slotIndex = i; break; }
        }
        if (slotIndex < 0)
            return -1;
    }
    if (slotIndex >= MAX_SLOTS)
        return -1;
    m_srvIndices[slotIndex] = srvIndex;
    if (slotIndex >= m_maxUsed)
        m_maxUsed = slotIndex + 1;
    return slotIndex;
}


bool TextureSlotInfo::Release(uint32_t srvIndex, int slotIndex) noexcept {
    if (slotIndex >= 0) {
        if (slotIndex < MAX_SLOTS && m_srvIndices[slotIndex] != srvIndex)
            return false;
        m_srvIndices[slotIndex] = 0u;
        return true;
    }
    bool released = false;
    for (int i = 0; i < m_maxUsed; ++i) {
        if (m_srvIndices[i] == srvIndex) {
            m_srvIndices[i] = 0u;
            released = true;
        }
    }
    return released;
}


uint32_t TextureSlotInfo::Query(int slotIndex) const noexcept {
    return (slotIndex >= 0 && slotIndex < MAX_SLOTS) ? m_srvIndices[slotIndex] : 0u;
}


bool TextureSlotInfo::Update(uint32_t srvIndex, int slotIndex) noexcept {
    if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return false;
    m_srvIndices[slotIndex] = srvIndex;
    if (slotIndex >= m_maxUsed) m_maxUsed = slotIndex + 1;
    return true;
}

// =================================================================================================
// GfxStates::Finish — Vulkan implementation.
//
// vkDeviceWaitIdle is a stronger barrier than CommandQueue::WaitIdle (vkQueueWaitIdle): it
// waits for ALL queues, not just the graphics queue. With separated graphics/present queues
// (see VKContext::SelectQueueFamilies) this matters when the caller is about to do something
// that needs all in-flight work done — Resize, Shutdown, RenderTarget::Create.
//
// CommandList::Flush keeps using CommandQueue::WaitIdle (queue-local).

void GfxStates::Finish(void) noexcept {
    if (vkContext.Device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(vkContext.Device());
}

// =================================================================================================
// API-neutral GfxStates methods (slot bookkeeping, ActiveState pass-through, ReleaseBuffers,
// ClearError, SetDrawBuffers). Bodies identical to the DX12 / OGL counterpart — no GPU calls.

RenderStates& GfxStates::ActiveState(void) noexcept {
    return baseRenderer.RenderStates();
}


TextureSlotInfo* GfxStates::FindInfo(GLenum typeTag) {
    for (auto& info : m_slotInfos)
        if (info.GetTypeTag() == typeTag)
            return &info;
    m_slotInfos.Append(TextureSlotInfo(typeTag));
    return &m_slotInfos[m_slotInfos.Length() - 1];
}


int GfxStates::BoundTMU(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    return (slotIndex >= 0) ? (info->Query(slotIndex) == srvIndex ? slotIndex : -1) : info->Find(srvIndex);
}


int GfxStates::BindTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    return info->Bind(srvIndex, slotIndex);
}


bool GfxStates::ReleaseTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return false;
    return info->Release(srvIndex, slotIndex);
}


int GfxStates::GetBoundTexture(GLenum typeTag, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return 0;
    return int(info->Query(slotIndex));
}


int GfxStates::SetBoundTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    info->Update(srvIndex, slotIndex);
    return slotIndex;
}


void GfxStates::ReleaseBuffers(void) noexcept {
    for (auto& info : m_slotInfos)
        info = TextureSlotInfo(info.GetTypeTag());
}


void GfxStates::SetDrawBuffers(const DrawBufferList& /*drawBuffers*/) {
    // no op — Vulkan binds attachments via vkCmdBeginRendering, not via per-draw mask.
}


void GfxStates::ClearError(void) noexcept {
    // no op — Vulkan has no equivalent of glGetError to drain. Validation messages are buffered
    // by the debug callback and cleared inside CheckError -> VKContext::DrainMessages.
}

// =================================================================================================
// Vulkan clear helpers. The clear runs outside any RenderPass via vkCmdClearColorImage /
// vkCmdClearDepthStencilImage — semantically identical to DX12 ClearRenderTargetView /
// ClearDepthStencilView at the call site (immediate clear of the supplied image).
// The image is transitioned to TRANSFER_DST_OPTIMAL via the ImageLayoutTracker; the caller
// is responsible for transitioning back to COLOR_ATTACHMENT / DEPTH_ATTACHMENT before the
// next draw (RenderTarget::Activate / Swapchain owners do this).

static VkImageSubresourceRange MakeColorRange(void) noexcept {
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = VK_REMAINING_MIP_LEVELS;
    range.baseArrayLayer = 0;
    range.layerCount = VK_REMAINING_ARRAY_LAYERS;
    return range;
}


static VkImageSubresourceRange MakeDepthRange(void) noexcept {
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    range.baseMipLevel = 0;
    range.levelCount = VK_REMAINING_MIP_LEVELS;
    range.baseArrayLayer = 0;
    range.layerCount = VK_REMAINING_ARRAY_LAYERS;
    return range;
}


static VkImageSubresourceRange MakeStencilRange(void) noexcept {
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    range.baseMipLevel = 0;
    range.levelCount = VK_REMAINING_MIP_LEVELS;
    range.baseArrayLayer = 0;
    range.layerCount = VK_REMAINING_ARRAY_LAYERS;
    return range;
}


void GfxStates::ClearColorBuffers(VkImage image, ImageLayoutTracker& tracker) noexcept {
    VkCommandBuffer cb = commandListHandler.CmdQueue().CmdBuffer();
    if (cb == VK_NULL_HANDLE or image == VK_NULL_HANDLE)
        return;

    tracker.ToTransferDst(cb);

    const float* src = m_clearColor.Data();
    VkClearColorValue value{};
    value.float32[0] = src[0];
    value.float32[1] = src[1];
    value.float32[2] = src[2];
    value.float32[3] = src[3];

    VkImageSubresourceRange range = MakeColorRange();
    vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
}


void GfxStates::ClearBackBuffer(const RGBAColor& color) noexcept {
    VkCommandBuffer cb = commandListHandler.CmdQueue().CmdBuffer();
    if (cb == VK_NULL_HANDLE)
        return;

    VkImage image = baseDisplayHandler.CurrentBackBuffer();
    if (image == VK_NULL_HANDLE)
        return;

    ImageLayoutTracker& tracker = baseDisplayHandler.CurrentBackBufferTracker();
    tracker.ToTransferDst(cb);

    const float* src = color.Data();
    VkClearColorValue value{};
    value.float32[0] = src[0];
    value.float32[1] = src[1];
    value.float32[2] = src[2];
    value.float32[3] = src[3];

    VkImageSubresourceRange range = MakeColorRange();
    vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
}


void GfxStates::ClearDepthBuffer(VkImage image, ImageLayoutTracker& tracker, float clearValue) noexcept {
    VkCommandBuffer cb = commandListHandler.CmdQueue().CmdBuffer();
    if (cb == VK_NULL_HANDLE or image == VK_NULL_HANDLE)
        return;

    tracker.ToTransferDst(cb);

    VkClearDepthStencilValue value{};
    value.depth = clearValue;
    value.stencil = 0;

    VkImageSubresourceRange range = MakeDepthRange();
    vkCmdClearDepthStencilImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
}


void GfxStates::ClearStencilBuffer(VkImage image, ImageLayoutTracker& tracker, int clearValue) noexcept {
    VkCommandBuffer cb = commandListHandler.CmdQueue().CmdBuffer();
    if (cb == VK_NULL_HANDLE or image == VK_NULL_HANDLE)
        return;

    tracker.ToTransferDst(cb);

    VkClearDepthStencilValue value{};
    value.depth = 0.0f;
    value.stencil = uint32_t(clearValue);

    VkImageSubresourceRange range = MakeStencilRange();
    vkCmdClearDepthStencilImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
}

// =================================================================================================
// SetMemoryBarrier — synchronization2 all-stages memory barrier. 1:1 to DX12's
// D3D12_RESOURCE_BARRIER_TYPE_UAV with pResource=nullptr ("flush all UAV writes").
// The Bitfield parameter is currently ignored (DX12 also ignores it — the call sites pass 0).

void GfxStates::SetMemoryBarrier(GfxTypes::Bitfield /*barriers*/) noexcept {
    VkCommandBuffer cb = commandListHandler.CmdQueue().CmdBuffer();
    if (cb == VK_NULL_HANDLE)
        return;

    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cb, &dep);
}

// =================================================================================================
// SetViewport — Y-flip via negative viewport.height (Vulkan 1.1 core / KHR_maintenance1).
// HLSL/DX12 expects clip-space Y pointing up; Vulkan's native NDC has Y pointing down.
// Setting viewport.y = top + height and viewport.height = -height pins the bottom-left corner
// to (left, top + height) and inverts the Y axis, so HLSL shaders ported to SPIR-V via DXC
// produce the same on-screen result as the DX12 path. Scissor stays in window-pixel coordinates
// (top-left origin, no flip).

void GfxStates::SetViewport(const GfxTypes::Int left, const GfxTypes::Int top, const GfxTypes::Int width, const GfxTypes::Int height) noexcept {
    m_viewport[0] = left;
    m_viewport[1] = top;
    m_viewport[2] = width;
    m_viewport[3] = height;

    VkCommandBuffer cb = commandListHandler.CmdQueue().CmdBuffer();
    if (cb == VK_NULL_HANDLE)
        return;

    VkViewport vp{};
    vp.x = float(left);
    vp.y = float(top + height);
    vp.width = float(width);
    vp.height = -float(height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset.x = (left < 0) ? 0 : left;
    scissor.offset.y = (top < 0) ? 0 : top;
    scissor.extent.width = (width < 0) ? 0u : uint32_t(width);
    scissor.extent.height = (height < 0) ? 0u : uint32_t(height);
    vkCmdSetScissor(cb, 0, 1, &scissor);
}

// =================================================================================================
// CheckError — central error sink. Drains the validation-layer log (filled by
// VkContextDebugCallback in vkcontext.cpp) and returns false if any error-severity entries
// were collected since the last call. 1:1 to DX12Context::DrainMessages-based CheckError.

bool GfxStates::CheckError(const char* operation) noexcept {
#ifdef _DEBUG
    int errors = vkContext.DrainMessages(false);
    if (errors > 0) {
        if (operation and *operation)
            fprintf(stderr, "GfxStates::CheckError: %d Vulkan validation error(s) at '%s'\n", errors, operation);
        else
            fprintf(stderr, "GfxStates::CheckError: %d Vulkan validation error(s)\n", errors);
        fflush(stderr);
        return false;
    }
#else
    (void)operation;
#endif
    return true;
}

// =================================================================================================
