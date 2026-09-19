#pragma once

#include <array>
#include <string>
#include <vector>

#include "dx12framework.h"
#include "string.hpp"
#include "basesingleton.hpp"
#include "avltree.hpp"
#include "array.hpp"
#include "rendertypes.h"

// =================================================================================================
// RenderStates: all PSO-relevant pipeline state encoded in a compact, hashable struct.

class Shader;

#pragma pack(push, 1)
struct RenderStates {
    using CompareFunc = GfxOperations::CompareFunc;
    using BlendFactor = GfxOperations::BlendFactor;
    using BlendOp = GfxOperations::BlendOp;
    using CullFace = GfxOperations::CullFace;
    using Winding = GfxOperations::Winding;
    using StencilOp = GfxOperations::StencilOp;
    using FillMode = GfxOperations::FillMode;

    // Rasterizer
    CullFace    cullMode{ CullFace::Back };
    uint8_t     faceCulling{ 1 };
    Winding     winding{ Winding::Regular };
    FillMode    fillMode{ FillMode::Solid };
    // Depth-stencil
    uint8_t     depthTest{ 1 };
    uint8_t     depthWrite{ 1 };
    CompareFunc depthFunc{ CompareFunc::LessEqual };
    uint8_t     stencilTest{ 0 };
    static constexpr int kColorTargets = 8;
    // Blend, one entry per color target
    uint8_t     blendEnable[kColorTargets]{ 0, 0, 0, 0, 0, 0, 0, 0 };
    BlendFactor blendSrcRGB[kColorTargets]{ BlendFactor::SrcAlpha, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One };
    BlendFactor blendDstRGB[kColorTargets]{ BlendFactor::InvSrcAlpha, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero };
    BlendFactor blendSrcAlpha[kColorTargets]{ BlendFactor::SrcAlpha, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One, BlendFactor::One };
    BlendFactor blendDstAlpha[kColorTargets]{ BlendFactor::InvSrcAlpha, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero, BlendFactor::Zero };
    BlendOp     blendOpRGB[kColorTargets]{ BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add };
    BlendOp     blendOpAlpha[kColorTargets]{ BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add, BlendOp::Add };
    // Independent blend (MRT passes that need a different blend per target, e.g. WBOIT: RT0 additive
    // accum, RT1 multiplicative revealage). 0 -> RT0's blend applies to all targets (default).
    uint8_t     independentBlend{ 0 };
    // Color mask (bit0=R bit1=G bit2=B bit3=A)
    uint8_t     colorMask[kColorTargets]{ 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F };
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
    // Stencil reference value and comparison (read) mask
    uint8_t     stencilRef{ 0 };
    uint8_t     stencilMask{ 0xFF };
    // Stencil write mask, separate from the comparison mask: a pass may test against the stencil without
    // writing it (see RenderTarget::dbmReadOnly, which needs both depth and stencil writes off so the
    // depth/stencil resource can be sampled while it stays bound).
    uint8_t     stencilWriteMask{ 0xFF };
    // Rasterizer depth clipping
    uint8_t     depthClip{ 1 };
    uint8_t     topology{ uint8_t(MeshTopology::Triangles) };
    // Polygon offset (OGL glPolygonOffset equivalent: factor -> slopeScaledDepthBias, units -> depthBias)
    int32_t     depthBias{ 0 };
    float       slopeScaledDepthBias{ 0.0f };
    // RTV formats of the render targets that are bound when the PSO is looked up (slot 0 in colorFormat,
    // slots 1+ in mrtFormats), and the DSV format (UNKNOWN without a DSV). Part of the memcmp'd PSO cache
    // key. Filled by PSO::GetPSO () from the active render target (RenderTarget::FillPipelineFormats ())
    // or the back buffer, so the PSO names exactly what OMSetRenderTargets bound.
    DXGI_FORMAT colorFormat{ DXGI_FORMAT_R8G8B8A8_UNORM };
    DXGI_FORMAT depthFormat{ DXGI_FORMAT_D32_FLOAT };
    DXGI_FORMAT mrtFormats[kColorTargets - 1]{};
    uint8_t     colorTargetCount{ 1 };

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

    struct PipelineLibrary {
        ComPtr<ID3D12PipelineLibrary>   library;
        std::vector<uint8_t>            data;
        String                          folder;
        bool                            dirty{ false };
    };

    struct PipelineRecords {
        AutoArray<String>       names;
        AutoArray<RenderStates> states;
        bool                    dirty{ false };
    };

private:
    static PSOCache& GetCache(PSOCache::Comparator comparator) noexcept {
        static PSOCache cache;
        cache.SetComparator(comparator);
        return cache;
    }

    static PipelineLibrary& GetLibrary(void) noexcept {
        static PipelineLibrary library;
        return library;
    }

    static PipelineRecords& GetRecords(void) noexcept {
        static PipelineRecords records;
        return records;
    }

    static int ComparePSOs(void* context, const PSOKey& key1, const PSOKey& key2);

    static int CompareShaders(void* context, const PSOKey& key1, const PSOKey& key2);

    static std::wstring PipelineName(const String& shaderName, uint64_t key);

    static HRESULT StorePipeline(const std::wstring& name, ID3D12PipelineState* pso) noexcept;

    static void NormalizeStates(RenderStates& states, int renderTargetCount) noexcept;

    static void Remember(const String& shaderName, const RenderStates& states) noexcept;

    static bool LoadRecords(const String& shaderFolder);

    static bool SaveRecords(void);

public:
    static psoPtr_t GetPSO(Shader* shader) noexcept;

    static void RemovePSOs(Shader* shader) noexcept;

    static bool LoadPipelineLibrary(const String& shaderFolder);

    static bool SavePipelineLibrary(void);

    static void PrecreatePSOs(void) noexcept;

    static bool CreateComputePipeline(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc, const String& shaderName, ID3DBlob* rootSignatureBlob, PSOComPtr& pso);

private:
    static PSOComPtr CreatePSO(Shader* shader, const RenderStates& states);
};

#define psoHandler PSOHandler::Instance()

// =================================================================================================
