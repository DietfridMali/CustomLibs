#pragma once

#include <array>

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "dictionary.hpp"
#include "rendertypes.h"

// =================================================================================================
// RenderState: all PSO-relevant pipeline state encoded in a compact, hashable struct.

class Shader;

struct RenderState {
    using CompareFunc = GfxOperations::CompareFunc;
    using BlendFactor = GfxOperations::BlendFactor;
    using BlendOp = GfxOperations::BlendOp;
    using FaceCull = GfxOperations::FaceCull;
    using Winding = GfxOperations::Winding;
    using StencilOp = GfxOperations::StencilOp;

    // Rasterizer
    FaceCull    cullMode{ FaceCull::Back };
    Winding     frontFace{ Winding::CW };
    // Depth-stencil
    uint8_t     depthTest{ 1 };
    uint8_t     depthWrite{ 1 };
    CompareFunc depthFunc{ CompareFunc::LessEqual };
    uint8_t     stencilTest{ 0 };
    // Blend (RT0)
    uint8_t     blendEnable{ 0 };
    BlendFactor blendSrcRGB{ BlendFactor::SrcAlpha };
    BlendFactor blendDstRGB{ BlendFactor::InvSrcAlpha };
    BlendFactor blendSrcAlpha{ BlendFactor::SrcAlpha };
    BlendFactor blendDstAlpha{ BlendFactor::InvSrcAlpha };
    BlendOp     blendOpRGB{ BlendOp::Add };
    BlendOp     blendOpAlpha{ BlendOp::Add };
    // Color mask (bit0=R bit1=G bit2=B bit3=A)
    uint8_t     colorMask{ 0x0F };
    // Scissor
    uint8_t     scissorTest{ 0 };
    // Stencil comparison (applied to both faces)
    CompareFunc stencilFunc{ CompareFunc::Always };
    // Front-face stencil operations
    StencilOp   stencilSFail{ StencilOp::Keep };
    StencilOp   stencilDPFail{ StencilOp::Keep };
    StencilOp   stencilDPPass{ StencilOp::Keep };
    // Back-face stencil operations (for single-pass two-sided algorithms)
    StencilOp   stencilBackSFail{ StencilOp::Keep };
    StencilOp   stencilBackDPFail{ StencilOp::Keep };
    StencilOp   stencilBackDPPass{ StencilOp::Keep };
    // Stencil reference value and mask
    uint8_t     stencilRef{ 0 };
    uint8_t     stencilMask{ 0xFF };

    bool operator==(const RenderState& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) == 0;
    }

    bool operator!=(const RenderState& o) const noexcept {
        return not (*this == o);
    }

    bool operator<(const RenderState& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) < 0;
    }
};

// =================================================================================================
// PSO cache key: {Shader*, RenderState} — used by CommandList's static PSO cache.

struct PSOKey {
    Shader* shader;
    RenderState state;
    bool operator<(const PSOKey& o) const noexcept {
        if (shader != o.shader)
            return shader < o.shader;
        return state < o.state;
    }
};

// =================================================================================================

class PSOHandler
    : public BaseSingleton<PSOHandler>
{
    using PSOCache = Dictionary<PSOKey, ComPtr<ID3D12PipelineState>>;

private:
    static PSOCache  m_psoCache;

public:
    ID3D12PipelineState* GetPSO(Shader* shader) noexcept;

    static void RemovePSO(Shader* shader) noexcept;

private:
    ComPtr<ID3D12PipelineState> CreatePSO(Shader* shader) noexcept;
};

#define psoHandler PSOHandler::Instance()

// =================================================================================================
