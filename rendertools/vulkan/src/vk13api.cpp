#include "vkframework.h"

#include "vk13api.h"

#include <cstdio>

// =================================================================================================

namespace
{
    PFN_vkCmdBeginRendering         pfnCmdBeginRendering        { nullptr };
    PFN_vkCmdEndRendering           pfnCmdEndRendering          { nullptr };
    PFN_vkCmdPipelineBarrier2       pfnCmdPipelineBarrier2      { nullptr };
    PFN_vkQueueSubmit2              pfnQueueSubmit2             { nullptr };
    PFN_vkCmdSetCullMode            pfnCmdSetCullMode           { nullptr };
    PFN_vkCmdSetFrontFace           pfnCmdSetFrontFace          { nullptr };
    PFN_vkCmdSetDepthTestEnable     pfnCmdSetDepthTestEnable    { nullptr };
    PFN_vkCmdSetDepthWriteEnable    pfnCmdSetDepthWriteEnable   { nullptr };
    PFN_vkCmdSetDepthCompareOp      pfnCmdSetDepthCompareOp     { nullptr };
    PFN_vkCmdSetDepthBiasEnable     pfnCmdSetDepthBiasEnable    { nullptr };
    PFN_vkCmdSetStencilTestEnable   pfnCmdSetStencilTestEnable  { nullptr };
    PFN_vkCmdSetStencilOp           pfnCmdSetStencilOp          { nullptr };

    bool g_loaded { false };

    template <typename PFN_T>
    bool LoadEntryPoint(VkDevice device, PFN_T& pfn, const char* coreName, const char* extName, bool useCore) noexcept
    {
        const char* name = useCore ? coreName : extName;
        pfn = reinterpret_cast<PFN_T>(vkGetDeviceProcAddr(device, name));
        if (pfn != nullptr)
            return true;
        fprintf(stderr, "Vk13Api::Load: entry point %s missing\n", name);
        return false;
    }
}

// =================================================================================================

bool Vk13Api::Load(VkDevice device, bool useCore) noexcept
{
    g_loaded = false;
    if (device == VK_NULL_HANDLE)
        return false;
    bool ok = true;
    ok = LoadEntryPoint(device, pfnCmdBeginRendering, "vkCmdBeginRendering", "vkCmdBeginRenderingKHR", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdEndRendering, "vkCmdEndRendering", "vkCmdEndRenderingKHR", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdPipelineBarrier2, "vkCmdPipelineBarrier2", "vkCmdPipelineBarrier2KHR", useCore) and ok;
    ok = LoadEntryPoint(device, pfnQueueSubmit2, "vkQueueSubmit2", "vkQueueSubmit2KHR", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdSetCullMode, "vkCmdSetCullMode", "vkCmdSetCullModeEXT", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdSetFrontFace, "vkCmdSetFrontFace", "vkCmdSetFrontFaceEXT", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdSetDepthTestEnable, "vkCmdSetDepthTestEnable", "vkCmdSetDepthTestEnableEXT", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdSetDepthWriteEnable, "vkCmdSetDepthWriteEnable", "vkCmdSetDepthWriteEnableEXT", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdSetDepthCompareOp, "vkCmdSetDepthCompareOp", "vkCmdSetDepthCompareOpEXT", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdSetDepthBiasEnable, "vkCmdSetDepthBiasEnable", "vkCmdSetDepthBiasEnableEXT", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdSetStencilTestEnable, "vkCmdSetStencilTestEnable", "vkCmdSetStencilTestEnableEXT", useCore) and ok;
    ok = LoadEntryPoint(device, pfnCmdSetStencilOp, "vkCmdSetStencilOp", "vkCmdSetStencilOpEXT", useCore) and ok;
    g_loaded = ok;
    return ok;
}


bool Vk13Api::IsLoaded(void) noexcept
{
    return g_loaded;
}


void Vk13Api::CmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo* info) noexcept
{
    pfnCmdBeginRendering(cb, info);
}


void Vk13Api::CmdEndRendering(VkCommandBuffer cb) noexcept
{
    pfnCmdEndRendering(cb);
}


void Vk13Api::CmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo* dependency) noexcept
{
    pfnCmdPipelineBarrier2(cb, dependency);
}


VkResult Vk13Api::QueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* submits, VkFence fence) noexcept
{
    return pfnQueueSubmit2(queue, submitCount, submits, fence);
}


void Vk13Api::CmdSetCullMode(VkCommandBuffer cb, VkCullModeFlags cullMode) noexcept
{
    pfnCmdSetCullMode(cb, cullMode);
}


void Vk13Api::CmdSetFrontFace(VkCommandBuffer cb, VkFrontFace frontFace) noexcept
{
    pfnCmdSetFrontFace(cb, frontFace);
}


void Vk13Api::CmdSetDepthTestEnable(VkCommandBuffer cb, VkBool32 enable) noexcept
{
    pfnCmdSetDepthTestEnable(cb, enable);
}


void Vk13Api::CmdSetDepthWriteEnable(VkCommandBuffer cb, VkBool32 enable) noexcept
{
    pfnCmdSetDepthWriteEnable(cb, enable);
}


void Vk13Api::CmdSetDepthCompareOp(VkCommandBuffer cb, VkCompareOp compareOp) noexcept
{
    pfnCmdSetDepthCompareOp(cb, compareOp);
}


void Vk13Api::CmdSetDepthBiasEnable(VkCommandBuffer cb, VkBool32 enable) noexcept
{
    pfnCmdSetDepthBiasEnable(cb, enable);
}


void Vk13Api::CmdSetStencilTestEnable(VkCommandBuffer cb, VkBool32 enable) noexcept
{
    pfnCmdSetStencilTestEnable(cb, enable);
}


void Vk13Api::CmdSetStencilOp(VkCommandBuffer cb, VkStencilFaceFlags faceMask, VkStencilOp failOp, VkStencilOp passOp, VkStencilOp depthFailOp, VkCompareOp compareOp) noexcept
{
    pfnCmdSetStencilOp(cb, faceMask, failOp, passOp, depthFailOp, compareOp);
}

// =================================================================================================
