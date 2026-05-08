#include "renderstates.h"

// =================================================================================================
// RenderStates — Vulkan pipeline-state subobject builders.
//
// The struct itself is API-neutral (memcmp-comparable). Mapping enums + Vk*StateCreateInfo
// fillers live here. The DX12 PSO class is replaced by PipelineCache (pipeline_cache.h).

// -------------------------------------------------------------------------------------------------
// Enum mappings

static VkBlendFactor ToVkBlend(GfxOperations::BlendFactor f) noexcept
{
    static const VkBlendFactor lut[] = {
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_SRC_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_FACTOR_DST_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
        VK_BLEND_FACTOR_DST_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
        VK_BLEND_FACTOR_ONE,
    };
    return lut[int(f)];
}


static VkBlendOp ToVkBlendOp(GfxOperations::BlendOp op) noexcept
{
    static const VkBlendOp lut[] = {
        VK_BLEND_OP_ADD,
        VK_BLEND_OP_SUBTRACT,
        VK_BLEND_OP_REVERSE_SUBTRACT,
        VK_BLEND_OP_MIN,
        VK_BLEND_OP_MAX,
    };
    return lut[int(op)];
}


static VkStencilOp ToVkStencilOp(GfxOperations::StencilOp op) noexcept
{
    static const VkStencilOp lut[] = {
        VK_STENCIL_OP_KEEP,
        VK_STENCIL_OP_ZERO,
        VK_STENCIL_OP_REPLACE,
        VK_STENCIL_OP_INCREMENT_AND_CLAMP,
        VK_STENCIL_OP_DECREMENT_AND_CLAMP,
        VK_STENCIL_OP_INCREMENT_AND_WRAP,
        VK_STENCIL_OP_DECREMENT_AND_WRAP,
    };
    return lut[int(op)];
}


static VkCompareOp ToVkCompareOp(GfxOperations::CompareFunc f) noexcept
{
    static const VkCompareOp lut[] = {
        VK_COMPARE_OP_NEVER,
        VK_COMPARE_OP_LESS,
        VK_COMPARE_OP_EQUAL,
        VK_COMPARE_OP_LESS_OR_EQUAL,
        VK_COMPARE_OP_GREATER,
        VK_COMPARE_OP_NOT_EQUAL,
        VK_COMPARE_OP_GREATER_OR_EQUAL,
        VK_COMPARE_OP_ALWAYS,
    };
    return lut[int(f)];
}


static VkCullModeFlags ToVkCullMode(GfxOperations::CullFace mode) noexcept
{
    switch (mode) {
        case GfxOperations::CullFace::Front: return VK_CULL_MODE_FRONT_BIT;
        case GfxOperations::CullFace::Back:  return VK_CULL_MODE_BACK_BIT;
        case GfxOperations::CullFace::None:  return VK_CULL_MODE_NONE;
    }
    return VK_CULL_MODE_NONE;
}


static VkFrontFace ToVkFrontFace(GfxOperations::Winding w) noexcept
{
    // DX12: FrontCounterClockwise = (winding == Reverse). Vulkan:
    //   COUNTER_CLOCKWISE = front-face is CCW.
    return (w == GfxOperations::Winding::Reverse)
         ? VK_FRONT_FACE_COUNTER_CLOCKWISE
         : VK_FRONT_FACE_CLOCKWISE;
}

// -------------------------------------------------------------------------------------------------

VkPipelineRasterizationStateCreateInfo& RenderStates::SetRasterizationInfo(VkPipelineRasterizationStateCreateInfo& info) const noexcept
{
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    info.depthClampEnable = depthClip ? VK_FALSE : VK_TRUE;  // Vk depthClampEnable is the inverse of DX12 DepthClip
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = VK_POLYGON_MODE_FILL;
    info.cullMode = ToVkCullMode(cullMode);
    info.frontFace = ToVkFrontFace(winding);
    info.depthBiasEnable = (depthBias != 0) or (slopeScaledDepthBias != 0.0f) ? VK_TRUE : VK_FALSE;
    info.depthBiasConstantFactor = float(depthBias);
    info.depthBiasClamp = 0.0f;
    info.depthBiasSlopeFactor = slopeScaledDepthBias;
    info.lineWidth = 1.0f;
    return info;
}


VkPipelineDepthStencilStateCreateInfo& RenderStates::SetDepthStencilInfo(VkPipelineDepthStencilStateCreateInfo& info) const noexcept
{
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    info.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
    info.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    info.depthCompareOp = ToVkCompareOp(depthFunc);
    info.depthBoundsTestEnable = VK_FALSE;
    info.stencilTestEnable = stencilTest ? VK_TRUE : VK_FALSE;

    info.front.failOp = ToVkStencilOp(stencilSFail);
    info.front.passOp = ToVkStencilOp(stencilDPPass);
    info.front.depthFailOp = ToVkStencilOp(stencilDPFail);
    info.front.compareOp = ToVkCompareOp(stencilFunc);
    info.front.compareMask = stencilMask;
    info.front.writeMask = stencilMask;
    info.front.reference = stencilRef;

    info.back.failOp = ToVkStencilOp(stencilBackSFail);
    info.back.passOp = ToVkStencilOp(stencilBackDPPass);
    info.back.depthFailOp = ToVkStencilOp(stencilBackDPFail);
    info.back.compareOp = ToVkCompareOp(stencilFunc);
    info.back.compareMask = stencilMask;
    info.back.writeMask = stencilMask;
    info.back.reference = stencilRef;

    info.minDepthBounds = 0.0f;
    info.maxDepthBounds = 1.0f;
    return info;
}


VkPipelineColorBlendAttachmentState& RenderStates::SetBlendAttachment(VkPipelineColorBlendAttachmentState& att) const noexcept
{
    att.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;
    att.srcColorBlendFactor = ToVkBlend(blendSrcRGB);
    att.dstColorBlendFactor = ToVkBlend(blendDstRGB);
    att.colorBlendOp = ToVkBlendOp(blendOpRGB);
    att.srcAlphaBlendFactor = ToVkBlend(blendSrcAlpha);
    att.dstAlphaBlendFactor = ToVkBlend(blendDstAlpha);
    att.alphaBlendOp = ToVkBlendOp(blendOpAlpha);

    att.colorWriteMask = 0;
    if (colorMask & 0x01) att.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if (colorMask & 0x02) att.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if (colorMask & 0x04) att.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if (colorMask & 0x08) att.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    return att;
}

// =================================================================================================
