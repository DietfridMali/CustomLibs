#include "renderstates.h"
#include "gfxstates.h"
#include "shader.h"
#include "dx12context.h"
#include "gfxrenderer.h"
#include "resource_view.h"


// =================================================================================================
// RenderStates::GetPSO — PSO creation helpers

static DXGI_FORMAT ToDXGIFormat(TextureFormat fmt) noexcept
{
    static DXGI_FORMAT lut[] = {
        DXGI_FORMAT_UNKNOWN,
        DXGI_FORMAT_R8_UNORM,
        DXGI_FORMAT_R8G8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32G32_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        DXGI_FORMAT_D32_FLOAT
    };
    return lut[int(fmt)];
}


static D3D12_BLEND ToD3DBlend(GfxOperations::BlendFactor factor) noexcept
{
    static D3D12_BLEND lut[] = {
        D3D12_BLEND_ZERO,
        D3D12_BLEND_ONE,
        D3D12_BLEND_SRC_COLOR,
        D3D12_BLEND_INV_SRC_COLOR,
        D3D12_BLEND_SRC_ALPHA,
        D3D12_BLEND_INV_SRC_ALPHA,
        D3D12_BLEND_DEST_ALPHA,
        D3D12_BLEND_INV_DEST_ALPHA,
        D3D12_BLEND_DEST_COLOR,
        D3D12_BLEND_INV_DEST_COLOR,
        D3D12_BLEND_ONE
    };
    return lut[int(factor)];
}


static D3D12_BLEND_OP ToD3DBlendOp(GfxOperations::BlendOp op) noexcept
{
    static D3D12_BLEND_OP lut[] = {
        D3D12_BLEND_OP_ADD,
        D3D12_BLEND_OP_SUBTRACT,
        D3D12_BLEND_OP_REV_SUBTRACT,
        D3D12_BLEND_OP_MIN,
        D3D12_BLEND_OP_MAX
    };
    return lut[int(op)];
}


static D3D12_STENCIL_OP ToD3DStencilOp(GfxOperations::StencilOp op) noexcept
{
    static D3D12_STENCIL_OP lut[] = {
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_ZERO,
        D3D12_STENCIL_OP_REPLACE,
        D3D12_STENCIL_OP_INCR_SAT,
        D3D12_STENCIL_OP_DECR_SAT,
        D3D12_STENCIL_OP_INCR,
        D3D12_STENCIL_OP_DECR
    };
    return lut[int(op)];
}


static D3D12_COMPARISON_FUNC ToD3DCompFunc(GfxOperations::CompareFunc func) noexcept
{
    static D3D12_COMPARISON_FUNC lut[] = {
        D3D12_COMPARISON_FUNC_NEVER,
        D3D12_COMPARISON_FUNC_LESS,
        D3D12_COMPARISON_FUNC_EQUAL,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_COMPARISON_FUNC_GREATER,
        D3D12_COMPARISON_FUNC_NOT_EQUAL,
        D3D12_COMPARISON_FUNC_GREATER_EQUAL,
        D3D12_COMPARISON_FUNC_ALWAYS
    };
    return lut[int(func)];
}


static D3D12_CULL_MODE ToD3DCullMode(GfxOperations::CullFace mode) noexcept
{
    static D3D12_CULL_MODE lut[] = {
        D3D12_CULL_MODE_FRONT,
        D3D12_CULL_MODE_BACK,
        D3D12_CULL_MODE_NONE
    };
    return lut[int(mode)];
}

// =================================================================================================

D3D12_RASTERIZER_DESC& RenderStates::SetRasterizerDesc(D3D12_RASTERIZER_DESC& desc) noexcept {
    desc.FillMode = D3D12_FILL_MODE_SOLID;
    desc.CullMode = ToD3DCullMode(cullMode);
    desc.FrontCounterClockwise = (winding == GfxOperations::Winding::Reverse) ? TRUE : FALSE;
    desc.DepthClipEnable = depthClip ? TRUE : FALSE;
    desc.MultisampleEnable = FALSE;
    desc.DepthBias = depthBias;
    desc.SlopeScaledDepthBias = slopeScaledDepthBias;
    desc.DepthBiasClamp = 0.0f;
    return desc;
}


D3D12_BLEND_DESC RenderStates::SetBlendDesc(D3D12_BLEND_DESC& desc) {
    desc.RenderTarget[0].RenderTargetWriteMask = colorMask;
    if (blendEnable) {
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = ToD3DBlend(blendSrcRGB);
        desc.RenderTarget[0].DestBlend = ToD3DBlend(blendDstRGB);
        desc.RenderTarget[0].BlendOp = ToD3DBlendOp(blendOpRGB);
        desc.RenderTarget[0].SrcBlendAlpha = ToD3DBlend(blendSrcAlpha);
        desc.RenderTarget[0].DestBlendAlpha = ToD3DBlend(blendDstAlpha);
        desc.RenderTarget[0].BlendOpAlpha = ToD3DBlendOp(blendOpAlpha);
    }
    return desc;
}


D3D12_DEPTH_STENCIL_DESC RenderStates::SetStencilDesc(D3D12_DEPTH_STENCIL_DESC& desc) {
    desc.DepthEnable = depthTest ? TRUE : FALSE;
    desc.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = ToD3DCompFunc(depthFunc);
    desc.StencilEnable = stencilTest ? TRUE : FALSE;
    desc.StencilReadMask = stencilMask;
    desc.StencilWriteMask = stencilMask;
    desc.FrontFace = { ToD3DStencilOp(stencilSFail), ToD3DStencilOp(stencilDPFail), ToD3DStencilOp(stencilDPPass), ToD3DCompFunc(stencilFunc) };
    desc.BackFace = { ToD3DStencilOp(stencilBackSFail), ToD3DStencilOp(stencilBackDPFail), ToD3DStencilOp(stencilBackDPPass), ToD3DCompFunc(stencilFunc) };
    return desc;
}

// =================================================================================================

int PSO::ComparePSOs(void* context, const PSOKey& key1, const PSOKey& key2) {
    return memcmp(&key1, &key2, sizeof(PSOKey));
}


int PSO::CompareShaders(void* context, const PSOKey& key1, const PSOKey& key2) {
    return memcmp(&key1.shader, &key2.shader, sizeof(Shader*));
}


PSO::psoPtr_t PSO::GetPSO(Shader* shader) noexcept
{
    if (not (shader and shader->IsValid()))
        return nullptr;

    PSOKey key{ shader, baseRenderer.RenderStates() };
    if (auto psoComPtr = GetCache(ComparePSOs).Find(key)) {
#ifdef _DEBUG
        GetCache(ComparePSOs).Find(key);
#endif
        return psoComPtr->Get();
    }
    
    PSO::PSOComPtr psoComPtr = CreatePSO(shader);
    if (psoComPtr) {
        psoPtr_t psoPtr = psoComPtr.Get();
#ifdef _DEBUG
        String psoName = String("shader:") + shader->m_name;
        psoComPtr->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)psoName.Length(), (const char*)psoName);
#endif
        if (GetCache(ComparePSOs).Insert(key, psoComPtr))
            return psoPtr;
    }
    return nullptr;
}



void PSO::RemovePSOs(Shader* shader) noexcept {
    PSOKey key{ shader };
    PSOCache cache = GetCache(CompareShaders);
    while (cache.Remove(key))
        ;
}


PSO::PSOComPtr PSO::CreatePSO(Shader* shader) noexcept
{
    ID3D12Device* device = dx12Context.Device();
    if (not (device and shader->m_rootSignature and shader->m_vsBlob and shader->m_psBlob))
        return nullptr;

    // Rasterizer
    
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = shader->m_rootSignature.Get();
    psoDesc.VS = { shader->m_vsBlob->GetBufferPointer(), shader->m_vsBlob->GetBufferSize() };
    psoDesc.PS = { shader->m_psBlob->GetBufferPointer(), shader->m_psBlob->GetBufferSize() };
    if (shader->m_gsBlob)
        psoDesc.GS = { shader->m_gsBlob->GetBufferPointer(), shader->m_gsBlob->GetBufferSize() };
    psoDesc.InputLayout = { shader->m_vsInputLayout.data(), UINT(shader->m_vsInputLayout.size()) };
    baseRenderer.RenderStates().SetRasterizerDesc(psoDesc.RasterizerState);
    baseRenderer.RenderStates().SetBlendDesc(psoDesc.BlendState);
    baseRenderer.RenderStates().SetStencilDesc(psoDesc.DepthStencilState);

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    int nrt = shader->m_dataLayout.m_numRenderTargets;
    psoDesc.NumRenderTargets = UINT(nrt);
    for (int i = 0; i < nrt; ++i)
        psoDesc.RTVFormats[i] = ToDXGIFormat(shader->m_dataLayout.m_rtvFormats[i]);
    psoDesc.DSVFormat = (psoDesc.DepthStencilState.DepthEnable or psoDesc.DepthStencilState.StencilEnable) ? dxDepthDSVFormat : DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;

    PSOComPtr psoComPtr;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&psoComPtr));
    if (FAILED(hr)) {
#ifdef _DEBUG
        fprintf(stderr, "RenderStates::CreatePSO '%s': PSO creation failed (hr=0x%08X)\n", (const char*)shader->m_name, (unsigned)hr);
#endif
        return nullptr;
    }
    return psoComPtr;
}

// =================================================================================================
