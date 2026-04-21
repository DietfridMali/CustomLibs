#include "renderstates.h"
#include "gfxstates.h"
#include "shader.h"
#include "dx12context.h"

PSO::PSOCache PSO::m_psoCache;

// =================================================================================================
// RenderStates::GetPSO — PSO creation helpers

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

// =================================================================================================

D3D12_RASTERIZER_DESC& RenderStates::SetRasterizerDesc(D3D12_RASTERIZER_DESC& desc) noexcept {
    desc.FillMode = D3D12_FILL_MODE_SOLID;
    desc.CullMode = (cullMode == GfxOperations::FaceCull::Back)
        ? D3D12_CULL_MODE_BACK
        : (cullMode == GfxOperations::FaceCull::Front)
        ? D3D12_CULL_MODE_FRONT
        : D3D12_CULL_MODE_NONE;
    desc.FrontCounterClockwise = (frontFace == GfxOperations::Winding::CCW) ? TRUE : FALSE;
    desc.DepthClipEnable = TRUE;
    desc.MultisampleEnable = FALSE;
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

ID3D12PipelineState* PSO::GetPSO(Shader* shader) noexcept
{
    if (not (shader and shader->IsValid()))
        return nullptr;

    PSOKey key{ shader, m_states };
    if (auto psoComPtr = m_psoCache.Find(key))
        return psoComPtr->Get();
    ComPtr<ID3D12PipelineState> psoComPtr = CreatePSO(shader);
    if (psoComPtr) {
        ID3D12PipelineState* psoPtr = psoComPtr.Get();
        m_psoCache.Insert(key, std::move(psoComPtr));
        return psoPtr;
    }
    return nullptr;
}


void PSO::RemovePSOs(Shader* shader) noexcept {
    for (auto it = m_psoCache.begin(); it != m_psoCache.end(); ) {
        if (it->first.shader == shader)
            it = m_psoCache.Erase(it);
        else
            ++it;
    }

}


ComPtr<ID3D12PipelineState> PSO::CreatePSO(Shader* shader) noexcept
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
    m_states.SetRasterizerDesc(psoDesc.RasterizerState);
    m_states.SetBlendDesc(psoDesc.BlendState);
    m_states.SetStencilDesc(psoDesc.DepthStencilState);

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = (psoDesc.DepthStencilState.DepthEnable or psoDesc.DepthStencilState.StencilEnable) ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> psoComPtr;
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
