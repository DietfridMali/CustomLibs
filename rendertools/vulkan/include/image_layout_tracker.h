#pragma once

#include "vkframework.h"

// =================================================================================================
// ImageLayoutTracker — Vulkan equivalent of DX12 D3D12_RESOURCE_STATES tracking.
//
// Each VkImage carries a current layout (e.g. UNDEFINED, COLOR_ATTACHMENT_OPTIMAL,
// SHADER_READ_ONLY_OPTIMAL, TRANSFER_DST_OPTIMAL, PRESENT_SRC_KHR). Vulkan additionally
// requires the producer pipeline stage and access type that wrote the image last, so the
// next consumer can synchronize precisely (instead of pessimistic full-pipeline barriers).
// The tracker keeps all three (layout, stage, access) and emits VkImageMemoryBarrier2 +
// vkCmdPipelineBarrier2 on transition. Caller passes only the destination layout/stage/access.
//
// Held by:
//   - class Texture (base) — inherited by every concrete texture (Wall, Smiley, Cubemap,
//     NoiseTexture3D, ShadowTexture, RenderTargetTexture, ...).
//   - class BufferInfo (per RT attachment) — replaces DX12 m_state + SetState mechanic.
//   - BaseDisplayHandler — one tracker per swapchain back buffer.
//
// Granularity: whole image (subresourceRange = ALL mips, ALL layers). Per-mip tracking
// (e.g. for explicit mipmap generation) can be added later as an extension.

class ImageLayoutTracker
{
public:
    VkImage                m_image    { VK_NULL_HANDLE };
    VkImageLayout          m_layout   { VK_IMAGE_LAYOUT_UNDEFINED };
    VkPipelineStageFlags2  m_stage    { VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT };
    VkAccessFlags2         m_access   { VK_ACCESS_2_NONE };
    VkImageAspectFlags     m_aspect   { VK_IMAGE_ASPECT_COLOR_BIT };

    // Records image handle + initial layout. Stage/access are reset to top-of-pipe / none.
    // Call from Texture::Create / BufferInfo::CreateColorBuffer / BaseDisplayHandler::AcquireBackBuffers.
    void Init(VkImage image, VkImageLayout initialLayout, VkImageAspectFlags aspect) noexcept;

    // Generic transition. No-op if newLayout == m_layout. Updates tracker state to dst values.
    void TransitionTo(VkCommandBuffer cb, VkImageLayout newLayout,
                      VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) noexcept;

    // Convenience wrappers for the common targets — encode the standard stage/access pairs.
    void ToShaderInput(VkCommandBuffer cb) noexcept;
    void ToColorAttachment(VkCommandBuffer cb) noexcept;
    void ToDepthAttachment(VkCommandBuffer cb) noexcept;
    void ToTransferDst(VkCommandBuffer cb) noexcept;
    void ToTransferSrc(VkCommandBuffer cb) noexcept;
    void ToPresent(VkCommandBuffer cb) noexcept;

    // Storage-image layout (read+write from compute / fragment shaders). Used by GfxArray.
    void ToGeneral(VkCommandBuffer cb) noexcept;

    inline VkImageLayout Layout(void) const noexcept { return m_layout; }
    inline VkPipelineStageFlags2 Stage(void) const noexcept { return m_stage; }
    inline VkAccessFlags2 Access(void) const noexcept { return m_access; }
};

// =================================================================================================
