#pragma once

#include <array>
#include <cstring>

#include "vkframework.h"
#include "rendertypes.h"

// =================================================================================================
// RenderStates: all pipeline-state values that contribute to the VkPipeline cache key.
// API-neutral struct — same shape as the DX12 version. Vulkan-specific subobject builders
// fill the matching Vk*StateCreateInfo from the current values.
//
// The DX12 PSO class (PSO::GetPSO / PSO::RemovePSOs / PSOKey) is replaced by the Vulkan
// PipelineCache (see pipeline_cache.h).

class Shader;

#pragma pack(push, 1)
struct RenderStates {
    using CompareFunc = GfxOperations::CompareFunc;
    using BlendFactor = GfxOperations::BlendFactor;
    using BlendOp = GfxOperations::BlendOp;
    using CullFace = GfxOperations::CullFace;
    using Winding = GfxOperations::Winding;
    using StencilOp = GfxOperations::StencilOp;

    // Rasterizer
    CullFace    cullMode { CullFace::Back };
    Winding     winding { Winding::Regular };
    // Depth-stencil
    uint8_t     depthTest { 1 };
    uint8_t     depthWrite { 1 };
    CompareFunc depthFunc { CompareFunc::LessEqual };
    uint8_t     stencilTest { 0 };
    // Blend (RT0)
    uint8_t     blendEnable { 0 };
    BlendFactor blendSrcRGB { BlendFactor::SrcAlpha };
    BlendFactor blendDstRGB { BlendFactor::InvSrcAlpha };
    BlendFactor blendSrcAlpha { BlendFactor::SrcAlpha };
    BlendFactor blendDstAlpha { BlendFactor::InvSrcAlpha };
    BlendOp     blendOpRGB { BlendOp::Add };
    BlendOp     blendOpAlpha { BlendOp::Add };
    // Color mask (bit0=R bit1=G bit2=B bit3=A)
    uint8_t     colorMask { 0x0F };
    // Scissor
    uint8_t     scissorTest { 0 };
    // Stencil comparison (applied to both faces)
    CompareFunc stencilFunc { CompareFunc::Always };
    // Front-face stencil operations
    StencilOp   stencilSFail { StencilOp::Keep };
    StencilOp   stencilDPFail { StencilOp::Keep };
    StencilOp   stencilDPPass { StencilOp::Keep };
    // Back-face stencil operations (for single-pass two-sided algorithms)
    StencilOp   stencilBackSFail { StencilOp::Keep };
    StencilOp   stencilBackDPFail { StencilOp::Keep };
    StencilOp   stencilBackDPPass { StencilOp::Keep };
    // Stencil reference value and mask
    uint8_t     stencilRef { 0 };
    uint8_t     stencilMask { 0xFF };
    // Rasterizer depth clipping
    uint8_t     depthClip { 1 };
    // Polygon offset (OGL glPolygonOffset equivalent: factor -> slopeScaledDepthBias, units -> depthBias)
    int32_t     depthBias { 0 };
    float       slopeScaledDepthBias { 0.0f };

    bool operator==(const RenderStates& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) == 0;
    }

    bool operator!=(const RenderStates& o) const noexcept {
        return not (*this == o);
    }

    bool operator<(const RenderStates& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) < 0;
    }

    // Vulkan pipeline-state subobject builders.

    // Fills in cullMode / frontFace / depthClampEnable / lineWidth etc.
    VkPipelineRasterizationStateCreateInfo& SetRasterizationInfo(VkPipelineRasterizationStateCreateInfo& info) const noexcept;

    // Fills in depthTestEnable / depthWriteEnable / depthCompareOp / stencil ops.
    VkPipelineDepthStencilStateCreateInfo& SetDepthStencilInfo(VkPipelineDepthStencilStateCreateInfo& info) const noexcept;

    // Fills in blend factors / ops / colorWriteMask for one color attachment.
    VkPipelineColorBlendAttachmentState& SetBlendAttachment(VkPipelineColorBlendAttachmentState& att) const noexcept;
};
#pragma pack(pop)

// =================================================================================================
