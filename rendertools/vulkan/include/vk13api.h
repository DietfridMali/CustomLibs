#pragma once

#include "vkframework.h"

// =================================================================================================

namespace Vk13Api
{
    bool Load(VkDevice device, bool useCore) noexcept;

    bool IsLoaded(void) noexcept;

    void CmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo* info) noexcept;

    void CmdEndRendering(VkCommandBuffer cb) noexcept;

    void CmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo* dependency) noexcept;

    VkResult QueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* submits, VkFence fence) noexcept;

    void CmdSetCullMode(VkCommandBuffer cb, VkCullModeFlags cullMode) noexcept;

    void CmdSetFrontFace(VkCommandBuffer cb, VkFrontFace frontFace) noexcept;

    void CmdSetDepthTestEnable(VkCommandBuffer cb, VkBool32 enable) noexcept;

    void CmdSetDepthWriteEnable(VkCommandBuffer cb, VkBool32 enable) noexcept;

    void CmdSetDepthCompareOp(VkCommandBuffer cb, VkCompareOp compareOp) noexcept;

    void CmdSetDepthBiasEnable(VkCommandBuffer cb, VkBool32 enable) noexcept;

    void CmdSetStencilTestEnable(VkCommandBuffer cb, VkBool32 enable) noexcept;

    void CmdSetStencilOp(VkCommandBuffer cb, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp) noexcept;
}

// =================================================================================================
