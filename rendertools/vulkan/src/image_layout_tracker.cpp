#include "image_layout_tracker.h"

// =================================================================================================
// ImageLayoutTracker

void ImageLayoutTracker::Init(VkImage image, VkImageLayout initialLayout, VkImageAspectFlags aspect) noexcept
{
    m_image  = image;
    m_layout = initialLayout;
    m_aspect = aspect;
    m_stage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    m_access = VK_ACCESS_2_NONE;
}


void ImageLayoutTracker::TransitionTo(VkCommandBuffer cb, VkImageLayout newLayout,
                                      VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) noexcept
{
    if (newLayout == m_layout)
        return;
    if ((m_image == VK_NULL_HANDLE) or (cb == VK_NULL_HANDLE))
        return;

    VkImageMemoryBarrier2 barrier { };
    barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask                = m_stage;
    barrier.srcAccessMask               = m_access;
    barrier.dstStageMask                = dstStage;
    barrier.dstAccessMask               = dstAccess;
    barrier.oldLayout                   = m_layout;
    barrier.newLayout                   = newLayout;
    barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                       = m_image;
    barrier.subresourceRange.aspectMask = m_aspect;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

    VkDependencyInfo dep { };
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cb, &dep);

    m_layout = newLayout;
    m_stage  = dstStage;
    m_access = dstAccess;
}


void ImageLayoutTracker::ToShaderRead(VkCommandBuffer cb) noexcept
{
    TransitionTo(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
}


void ImageLayoutTracker::ToColorAttachment(VkCommandBuffer cb) noexcept
{
    TransitionTo(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}


void ImageLayoutTracker::ToDepthAttachment(VkCommandBuffer cb) noexcept
{
    // Both early and late fragment tests touch the depth buffer; cover both.
    TransitionTo(cb, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
}


void ImageLayoutTracker::ToTransferDst(VkCommandBuffer cb) noexcept
{
    TransitionTo(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
}


void ImageLayoutTracker::ToTransferSrc(VkCommandBuffer cb) noexcept
{
    TransitionTo(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
}


void ImageLayoutTracker::ToPresent(VkCommandBuffer cb) noexcept
{
    // Present has no consuming stage we can wait on inside this command buffer; signal
    // through bottom-of-pipe with no access. The actual present-vs-render sync happens
    // via the swapchain's renderFinished semaphore (CommandQueue::Present).
    TransitionTo(cb, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE);
}


void ImageLayoutTracker::ToGeneral(VkCommandBuffer cb) noexcept
{
    // Storage image: read + write from any shader stage. Cover all shader stages plus
    // SHADER_STORAGE access (read & write) so subsequent vertex/fragment/compute uses are safe.
    TransitionTo(cb, VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

// =================================================================================================
