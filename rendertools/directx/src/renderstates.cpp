#include "renderstates.h"

// =================================================================================================

ID3D12PipelineState* PSOHandler::GetPSO(Shader* shader) noexcept
{
    if (not (shader and shader->IsValid()))
        return nullptr;

    PSOKey key{ shader, *this };
    if (auto* psoComPtr = m_psoCache.Find(key))
        return (*psoComPtr)->Get();
    ComPtr<ID3D12PipelineState> psoComPtr = CreatePSO(shader);
    if (psoComPtr) {
        ID3D12PipelineState* psoPtr = psoComPtr.Get();
        m_psoCache.Insert(key, std::move(psoComPtr));
        return psoComPtr.Get();
    }
    return nullptr;
}


PSOHandler::RemovePSO(Shader* shader) noexcept {
    for (auto it = m_psoCache.begin(); it != m_psoCache.end(); ) {
        if (it->first.shader == this)
            it = m_psoCache.Erase(it);
        else
            ++it;
    }

}


ComPtr<ID3D12PipelineState> PSOHandler::CreatePSO(Shader* shader) noexcept
{
    ID3D12Device* device = dx12Context.Device();
    if (not (device and shader->m_rootSignature and shader->m_vsBlob and shader->m_psBlob))
        return nullptr;

    // Rasterizer
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = (cullMode == GfxOperations::FaceCull::Back)
        ? D3D12_CULL_MODE_BACK
        : (cullMode == GfxOperations::FaceCull::Front)
        ? D3D12_CULL_MODE_FRONT
        : D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = (frontFace == GfxOperations::Winding::CCW) ? TRUE : FALSE;
    rast.DepthClipEnable = TRUE;
    rast.MultisampleEnable = FALSE;

    // Blend
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = colorMask;
    if (blendEnable) {
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = ToD3DBlend(blendSrcRGB);
        blend.RenderTarget[0].DestBlend = ToD3DBlend(blendDstRGB);
        blend.RenderTarget[0].BlendOp = ToD3DBlendOp(blendOpRGB);
        blend.RenderTarget[0].SrcBlendAlpha = ToD3DBlend(blendSrcAlpha);
        blend.RenderTarget[0].DestBlendAlpha = ToD3DBlend(blendDstAlpha);
        blend.RenderTarget[0].BlendOpAlpha = ToD3DBlendOp(blendOpAlpha);
    }

    // Depth / stencil
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = depthTest ? TRUE : FALSE;
    ds.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = ToD3DCompFunc(depthFunc);
    ds.StencilEnable = stencilTest ? TRUE : FALSE;
    ds.StencilReadMask = stencilMask;
    ds.StencilWriteMask = stencilMask;
    ds.FrontFace = { ToD3DStencilOp(stencilSFail), ToD3DStencilOp(stencilDPFail), ToD3DStencilOp(stencilDPPass), ToD3DCompFunc(stencilFunc) };
    ds.BackFace = { ToD3DStencilOp(stencilBackSFail), ToD3DStencilOp(stencilBackDPFail), ToD3DStencilOp(stencilBackDPPass), ToD3DCompFunc(stencilFunc) };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = shader->m_rootSignature.Get();
    psoDesc.VS = { shader->m_vsBlob->GetBufferPointer(), shader->m_vsBlob->GetBufferSize() };
    psoDesc.PS = { shader->m_psBlob->GetBufferPointer(), shader->m_psBlob->GetBufferSize() };
    if (shader->m_gsBlob)
        psoDesc.GS = { shader->m_gsBlob->GetBufferPointer(), shader->m_gsBlob->GetBufferSize() };
    psoDesc.InputLayout = { shader->m_vsInputLayout.data(), UINT(shader->m_vsInputLayout.size()) };
    psoDesc.RasterizerState = rast;
    psoDesc.BlendState = blend;
    psoDesc.DepthStencilState = ds;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = (ds.DepthEnable or ds.StencilEnable) ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> psoComPtr;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&psoComPtr));
    if (FAILED(hr)) {
#ifdef _DEBUG
        fprintf(stderr, "RenderState::CreatePSO '%s': PSO creation failed (hr=0x%08X)\n", (const char*)shader->m_name, (unsigned)hr);
#endif
        return nullptr;
    }
    return psoComPtr;
}

// =================================================================================================
