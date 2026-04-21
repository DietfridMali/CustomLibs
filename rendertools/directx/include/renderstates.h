#pragma once

#include <array>

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "avltree.hpp"
#include "rendertypes.h"

// =================================================================================================
// RenderStates: all PSO-relevant pipeline state encoded in a compact, hashable struct.

class Shader;

#pragma pack(push, 1)
struct RenderStates {
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

    bool operator==(const RenderStates& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) == 0;
    }

    bool operator!=(const RenderStates& o) const noexcept {
        return not (*this == o);
    }

    bool operator<(const RenderStates& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) < 0;
    }

    D3D12_RASTERIZER_DESC& SetRasterizerDesc(D3D12_RASTERIZER_DESC& desc) noexcept;

    D3D12_BLEND_DESC SetBlendDesc(D3D12_BLEND_DESC& desc);

    D3D12_DEPTH_STENCIL_DESC SetStencilDesc(D3D12_DEPTH_STENCIL_DESC& desc);
};
#pragma pack(pop)

// =================================================================================================
// PSO cache key: {Shader*, RenderStates} — used by CommandList's static PSO cache.

#pragma pack(push, 1)
struct PSOKey {
    Shader* shader{ nullptr };
    RenderStates states;
#if 0 // only required for StdMap, not for AVLTree
    bool operator<(const PSOKey& o) const noexcept {
        if (shader != o.shader)
            return shader < o.shader;
        return states < o.states;
    }
#endif
};
#pragma pack(pop)

// =================================================================================================

class PSO
{
    using PSOComPtr = ComPtr<ID3D12PipelineState>;
    typedef ID3D12PipelineState* psoPtr_t;
    using PSOCache = AVLTree<PSOKey, PSOComPtr>;

private:
    static PSOCache& GetCache(PSOCache::Comparator comparator) noexcept {
        static PSOCache cache;
        cache.SetComparator(comparator);
        return cache;
    }

    static int ComparePSOs(void* context, const PSOKey& key1, const PSOKey& key2);

    static int CompareShaders(void* context, const PSOKey& key1, const PSOKey& key2);

public:
    static psoPtr_t GetPSO(Shader* shader) noexcept;

    static void RemovePSOs(Shader* shader) noexcept;

private:
    static PSOComPtr CreatePSO(Shader* shader) noexcept;
};

#define psoHandler PSOHandler::Instance()

// =================================================================================================
