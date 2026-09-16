#include "renderstates.h"
#include "gfxpixelformat_dx.h"
#include "gfxstates.h"
#include "shader.h"
#include "dx12context.h"
#include "gfxrenderer.h"
#include "resource_view.h"
#include "shadercache.h"

#include <cwchar>


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
        DXGI_FORMAT_D32_FLOAT,
        DXGI_FORMAT_R16_UINT,
        DXGI_FORMAT_R32_UINT
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
    desc.FillMode = (fillMode == GfxOperations::FillMode::Wireframe) ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
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
    desc.RenderTarget[0].RenderTargetWriteMask = colorMask[0];
    if (blendEnable[0]) {
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = ToD3DBlend(blendSrcRGB[0]);
        desc.RenderTarget[0].DestBlend = ToD3DBlend(blendDstRGB[0]);
        desc.RenderTarget[0].BlendOp = ToD3DBlendOp(blendOpRGB[0]);
        desc.RenderTarget[0].SrcBlendAlpha = ToD3DBlend(blendSrcAlpha[0]);
        desc.RenderTarget[0].DestBlendAlpha = ToD3DBlend(blendDstAlpha[0]);
        desc.RenderTarget[0].BlendOpAlpha = ToD3DBlendOp(blendOpAlpha[0]);
    }
    // Independent RT1 blend (e.g. WBOIT: RT0 additive accum, RT1 multiplicative revealage). Only when
    // requested; otherwise IndependentBlendEnable stays FALSE and RT0's blend replicates to all targets.
    if (independentBlend) {
        desc.IndependentBlendEnable = TRUE;
        desc.RenderTarget[1].RenderTargetWriteMask = colorMask[1];
        if (blendEnable[1]) {
            desc.RenderTarget[1].BlendEnable = TRUE;
            desc.RenderTarget[1].SrcBlend = ToD3DBlend(blendSrcRGB[1]);
            desc.RenderTarget[1].DestBlend = ToD3DBlend(blendDstRGB[1]);
            desc.RenderTarget[1].BlendOp = ToD3DBlendOp(blendOpRGB[1]);
            desc.RenderTarget[1].SrcBlendAlpha = ToD3DBlend(blendSrcAlpha[1]);
            desc.RenderTarget[1].DestBlendAlpha = ToD3DBlend(blendDstAlpha[1]);
            desc.RenderTarget[1].BlendOpAlpha = ToD3DBlendOp(blendOpAlpha[1]);
        }
        // RT2 (3-MRT G-buffer worldPos) must stay an opaque full write under independent blending: once
        // IndependentBlendEnable stops RT0's replication, the zero-initialised RT2 desc has WriteMask 0 and
        // would stop writing. worldPos must never blend (its .w is gloss, not opacity). Harmless for <=2-RT
        // independent passes (WBOIT only touches RT0/RT1).
        desc.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    return desc;
}


D3D12_DEPTH_STENCIL_DESC RenderStates::SetStencilDesc(D3D12_DEPTH_STENCIL_DESC& desc) {
    desc.DepthEnable = depthTest ? TRUE : FALSE;
    desc.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = ToD3DCompFunc(depthFunc);
    desc.StencilEnable = stencilTest ? TRUE : FALSE;
    desc.StencilReadMask = stencilMask;
    desc.StencilWriteMask = stencilWriteMask;
    desc.FrontFace = { ToD3DStencilOp(stencilSFail), ToD3DStencilOp(stencilDPFail), ToD3DStencilOp(stencilDPPass), ToD3DCompFunc(stencilFunc) };
    desc.BackFace = { ToD3DStencilOp(stencilBackSFail), ToD3DStencilOp(stencilBackDPFail), ToD3DStencilOp(stencilBackDPPass), ToD3DCompFunc(stencilFunc) };
    return desc;
}

// =================================================================================================

static uint64_t PipelineKey(const Shader* shader, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) noexcept
{
    uint64_t key = ShaderCache::kHashSeed;
    key = ShaderCache::Hash(key, desc.VS.pShaderBytecode, desc.VS.BytecodeLength);
    key = ShaderCache::Hash(key, desc.PS.pShaderBytecode, desc.PS.BytecodeLength);
    if (desc.GS.pShaderBytecode)
        key = ShaderCache::Hash(key, desc.GS.pShaderBytecode, desc.GS.BytecodeLength);
    if (desc.HS.pShaderBytecode)
        key = ShaderCache::Hash(key, desc.HS.pShaderBytecode, desc.HS.BytecodeLength);
    if (desc.DS.pShaderBytecode)
        key = ShaderCache::Hash(key, desc.DS.pShaderBytecode, desc.DS.BytecodeLength);
    key = ShaderCache::Hash(key, shader->m_rootSignatureBlob->GetBufferPointer(), shader->m_rootSignatureBlob->GetBufferSize());
    for (UINT i = 0; i < desc.InputLayout.NumElements; ++i) {
        const D3D12_INPUT_ELEMENT_DESC& e = desc.InputLayout.pInputElementDescs[i];
        key = ShaderCache::Hash(key, e.SemanticName);
        key = ShaderCache::Hash(key, &e.SemanticIndex, sizeof(e.SemanticIndex));
        key = ShaderCache::Hash(key, &e.Format, sizeof(e.Format));
        key = ShaderCache::Hash(key, &e.InputSlot, sizeof(e.InputSlot));
        key = ShaderCache::Hash(key, &e.AlignedByteOffset, sizeof(e.AlignedByteOffset));
        key = ShaderCache::Hash(key, &e.InputSlotClass, sizeof(e.InputSlotClass));
        key = ShaderCache::Hash(key, &e.InstanceDataStepRate, sizeof(e.InstanceDataStepRate));
    }
    const RenderStates& states = baseRenderer.RenderStates();
    key = ShaderCache::Hash(key, &states, sizeof(RenderStates));
    key = ShaderCache::Hash(key, &desc.PrimitiveTopologyType, sizeof(desc.PrimitiveTopologyType));
    key = ShaderCache::Hash(key, &desc.NumRenderTargets, sizeof(desc.NumRenderTargets));
    key = ShaderCache::Hash(key, desc.RTVFormats, sizeof(desc.RTVFormats[0]) * desc.NumRenderTargets);
    key = ShaderCache::Hash(key, &desc.DSVFormat, sizeof(desc.DSVFormat));
    return key;
}


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
#if DBG_DIRECTX
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
    PSOCache& cache = GetCache(CompareShaders);
    while (cache.Remove(key))
        ;
}


std::wstring PSO::PipelineName(const String& shaderName, uint64_t key)
{
    wchar_t hex[17];
    std::swprintf(hex, 17, L"%016llx", static_cast<unsigned long long>(key));
    std::wstring name;
    for (const char* c = static_cast<const char*>(shaderName); *c; ++c)
        name.push_back(wchar_t(uint8_t(*c)));
    name.push_back(L'_');
    name.append(hex);
    return name;
}


HRESULT PSO::StorePipeline(const std::wstring& name, ID3D12PipelineState* pso) noexcept
{
    PipelineLibrary& lib = GetLibrary();
    HRESULT hr = lib.library->StorePipeline(name.c_str(), pso);
    if (SUCCEEDED(hr))
        lib.dirty = true;
    return hr;
}


bool PSO::LoadPipelineLibrary(const String& shaderFolder)
{
    PipelineLibrary& lib = GetLibrary();
    lib.library.Reset();
    lib.data.clear();
    lib.dirty = false;
    lib.folder = shaderFolder;
    if (shaderFolder.IsEmpty())
        return false;
    ComPtr<ID3D12Device1> device;
    if (FAILED(dx12Context.Device()->QueryInterface(IID_PPV_ARGS(device.GetAddressOf()))))
        return false;
    if (ShaderCache::ReadFile(shaderFolder, String("pipelines.d3d12"), lib.data)
        and SUCCEEDED(device->CreatePipelineLibrary(lib.data.data(), lib.data.size(), IID_PPV_ARGS(lib.library.GetAddressOf()))))
        return true;
    lib.data.clear();
    return SUCCEEDED(device->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(lib.library.ReleaseAndGetAddressOf())));
}


bool PSO::SavePipelineLibrary(void)
{
    PipelineLibrary& lib = GetLibrary();
    if (not (lib.library and lib.dirty))
        return true;
    size_t size = lib.library->GetSerializedSize();
    std::vector<uint8_t> data(size, 0);
    if (FAILED(lib.library->Serialize(data.data(), size)))
        return false;
    if (not ShaderCache::WriteFile(lib.folder, String("pipelines.d3d12"), data.data(), size))
        return false;
    lib.dirty = false;
    return true;
}


bool PSO::CreateComputePipeline(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc, const String& shaderName, ID3DBlob* rootSignatureBlob, PSOComPtr& pso)
{
    PipelineLibrary& lib = GetLibrary();
    if (not lib.library)
        return SUCCEEDED(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf())));
    uint64_t key = ShaderCache::Hash(ShaderCache::kHashSeed, desc.CS.pShaderBytecode, desc.CS.BytecodeLength);
    key = ShaderCache::Hash(key, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize());
    std::wstring name = PipelineName(shaderName, key);
    if (SUCCEEDED(lib.library->LoadComputePipeline(name.c_str(), &desc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf()))))
        return true;
    if (FAILED(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf()))))
        return false;
    StorePipeline(name, pso.Get());
    return true;
}


PSO::PSOComPtr PSO::CreatePSO(Shader* shader)
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
    if (shader->IsTessellated()) {
        psoDesc.HS = { shader->m_hsBlob->GetBufferPointer(), shader->m_hsBlob->GetBufferSize() };
        psoDesc.DS = { shader->m_dsBlob->GetBufferPointer(), shader->m_dsBlob->GetBufferSize() };
    }
    psoDesc.InputLayout = { shader->m_vsInputLayout.data(), UINT(shader->m_vsInputLayout.size()) };
    baseRenderer.RenderStates().SetRasterizerDesc(psoDesc.RasterizerState);
    baseRenderer.RenderStates().SetBlendDesc(psoDesc.BlendState);
    baseRenderer.RenderStates().SetStencilDesc(psoDesc.DepthStencilState);

    psoDesc.PrimitiveTopologyType = shader->IsTessellated() ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    int nrt = shader->m_dataLayout.m_numRenderTargets;
    psoDesc.NumRenderTargets = UINT(nrt);
    // Slot 0 (color) follows the active render target's color format (RenderStates::colorFormat, set
    // in RenderTarget::Enable) so one shader serves both the RGBA8 screen and the HDR scene buffer.
    // Slots 1+ (worldNormal/worldPos MRTs) keep their shader-declared formats.
    for (int i = 0; i < nrt; ++i)
        psoDesc.RTVFormats[i] = (i == 0) ? baseRenderer.RenderStates().colorFormat : ToDXGIFormat(shader->m_dataLayout.m_rtvFormats[i]);
    for (int i = 1; i < nrt; ++i) {
        if (IsIntegerColorFormat(psoDesc.RTVFormats[i]) and psoDesc.BlendState.RenderTarget[0].BlendEnable and not psoDesc.BlendState.IndependentBlendEnable) {
            psoDesc.BlendState.IndependentBlendEnable = TRUE;
            for (int j = 1; j < nrt; ++j)
                psoDesc.BlendState.RenderTarget[j] = psoDesc.BlendState.RenderTarget[0];
            break;
        }
    }
    for (int i = 0; i < nrt; ++i) {
        if (IsIntegerColorFormat(psoDesc.RTVFormats[i]))
            psoDesc.BlendState.RenderTarget[i].BlendEnable = FALSE;
    }
    // Must match the DSV bound by the active render target, combined depth/stencil included — see
    // RenderStates::depthFormat.
    psoDesc.DSVFormat = (psoDesc.DepthStencilState.DepthEnable or psoDesc.DepthStencilState.StencilEnable)
                      ? baseRenderer.RenderStates().depthFormat
                      : DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;

    PSOComPtr psoComPtr;
    PipelineLibrary& lib = GetLibrary();
    std::wstring name;
    if (lib.library) {
        name = PipelineName(shader->m_name, PipelineKey(shader, psoDesc));
        if (SUCCEEDED(lib.library->LoadGraphicsPipeline(name.c_str(), &psoDesc, IID_PPV_ARGS(&psoComPtr))))
            return psoComPtr;
    }
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&psoComPtr));
    if (FAILED(hr)) {
#ifdef _DEBUG
        fprintf(stderr, "RenderStates::CreatePSO '%s': PSO creation failed (hr=0x%08X)\n", (const char*)shader->m_name, (unsigned)hr);
#endif
        return nullptr;
    }
    if (lib.library)
        StorePipeline(name, psoComPtr.Get());
    return psoComPtr;
}

// =================================================================================================
