#define NOMINMAX

#include <utility>
#include <cstring>
#include <ranges>
#include <string_view>

#include "framework.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>

#include "shader.h"
#include "shadowmap.h"
#include "base_renderer.h"
#include "command_queue.h"
#include "descriptor_heap.h"
#include "dx12context.h"
#include "gfxstates.h"

#pragma comment(lib, "d3dcompiler.lib")

// =================================================================================================
// DX12 Shader implementation
//
// Root signature layout (fixed for all shaders):
//   Param 0: Root CBV  — b0 FrameConstants     (vertex + pixel)
//   Param 1: Root CBV  — b1 ShaderConstants    (vertex + pixel)
//   Param 2: Desc table — t0..t15 SRVs         (pixel)
//   Static samplers: s0 = linear clamp, s1 = linear repeat (pixel)
//
// PSO cache: keyed by RenderState bitmask; created on first Enable() for that state.

// -------------------------------------------------------------------------------------------------
// Fixed input layout (must match the HLSL vertex shader input semantics)

static const D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    4, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       5, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};
static constexpr UINT kInputLayoutCount = UINT(std::size(kInputLayout));

// -------------------------------------------------------------------------------------------------
// Helper: create a committed upload-heap CB resource of the given size (256-byte aligned).

static ComPtr<ID3D12Resource> CreateUploadCB(ID3D12Device* device, uint32_t size) noexcept
{
    const UINT aligned = (size + 255u) & ~255u;
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width     = aligned;
    rd.Height    = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout    = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> res;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
    return res;
}

// -------------------------------------------------------------------------------------------------

static void UploadCBData(ID3D12Resource* resource, const void* data, uint32_t size) noexcept
{
    if (!resource || !data || size == 0) return;
    void* mapped = nullptr;
    D3D12_RANGE range{ 0, 0 };
    if (SUCCEEDED(resource->Map(0, &range, &mapped))) {
        std::memcpy(mapped, data, size);
        resource->Unmap(0, nullptr);
    }
}

// =================================================================================================

bool Shader::Compile(const char* hlslCode, const char* entryPoint, const char* target,
                     ComPtr<ID3DBlob>& blobOut) noexcept
{
    if (!hlslCode || !*hlslCode) return false;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3DCompile(hlslCode, strlen(hlslCode), (const char*)m_name,
                            nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            entryPoint, target, flags, 0,
                            &blobOut, &errBlob);
    if (FAILED(hr)) {
#ifdef _DEBUG
        if (errBlob)
            fprintf(stderr, "Shader '%s' (%s) compile error:\n%s\n",
                    (const char*)m_name, target,
                    static_cast<const char*>(errBlob->GetBufferPointer()));
#endif
        return false;
    }
    return true;
}


bool Shader::CreateRootSignature(void) noexcept
{
    ID3D12Device* device = dx12Context.Device();
    if (!device) return false;

    // Param 2: descriptor table for 16 SRVs (t0..t15)
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors     = 16;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace      = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3]{};

    // Root CBV b0 — FrameConstants
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].ShaderVisibility           = D3D12_SHADER_VISIBILITY_ALL;

    // Root CBV b1 — ShaderConstants
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].ShaderVisibility           = D3D12_SHADER_VISIBILITY_ALL;

    // Descriptor table SRV t0..t15
    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[2].ShaderVisibility                     = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static samplers: s0 = linear clamp, s1 = linear repeat
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (int i = 0; i < 2; ++i) {
        samplers[i].Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[i].AddressU       =
        samplers[i].AddressV       =
        samplers[i].AddressW       = (i == 0) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
                                               : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].MaxAnisotropy  = 1;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[i].MinLOD         = 0.0f;
        samplers[i].MaxLOD         = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = UINT(i);
        samplers[i].RegisterSpace  = 0;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters     = 3;
    rsd.pParameters       = params;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers   = samplers;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
              | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
              | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
              | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
#ifdef _DEBUG
        if (err)
            fprintf(stderr, "Shader '%s': root signature serialization error:\n%s\n",
                    (const char*)m_name,
                    static_cast<const char*>(err->GetBufferPointer()));
#endif
        return false;
    }
    return SUCCEEDED(device->CreateRootSignature(0,
        sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)));
}


bool Shader::CreateCBs(void) noexcept
{
    ID3D12Device* device = dx12Context.Device();
    if (!device) return false;

    // b0 — fixed 256 bytes
    m_b0Buffer = CreateUploadCB(device, sizeof(FrameConstants));
    if (!m_b0Buffer) return false;

    // b1 — size from reflection (0 if shader has no b1 CB)
    if (m_b1Size > 0) {
        m_b1Buffer = CreateUploadCB(device, m_b1Size);
        if (!m_b1Buffer) return false;
        m_b1Staging.assign(m_b1Size, 0);
    }
    return true;
}


bool Shader::Create(const String& vsCode, const String& fsCode, const String& /*gsCode*/)
{
    Destroy();

    if (!Compile((const char*)vsCode, "VSMain", "vs_5_0", m_vsBlob)) return false;
    if (!Compile((const char*)fsCode, "PSMain", "ps_5_0", m_psBlob)) return false;

    // Reflect b1 fields from PS blob (look for cbuffer named "ShaderConstants")
    {
        ComPtr<ID3D12ShaderReflection> refl;
        if (SUCCEEDED(D3DReflect(m_psBlob->GetBufferPointer(), m_psBlob->GetBufferSize(),
                                 IID_PPV_ARGS(&refl)))) {
            D3D12_SHADER_DESC sd{};
            refl->GetDesc(&sd);
            for (UINT i = 0; i < sd.ConstantBuffers; ++i) {
                ID3D12ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(i);
                D3D12_SHADER_BUFFER_DESC cbd{};
                cb->GetDesc(&cbd);
                if (strcmp(cbd.Name, "ShaderConstants") == 0) {
                    m_b1Size = cbd.Size;
                    for (UINT j = 0; j < cbd.Variables; ++j) {
                        ID3D12ShaderReflectionVariable* var = cb->GetVariableByIndex(j);
                        D3D12_SHADER_VARIABLE_DESC vd{};
                        var->GetDesc(&vd);
                        if (vd.uFlags & D3D_SVF_USED) {
                            auto* entry = m_b1Fields.Append();
                            if (entry)
                                *entry = { String(vd.Name), { vd.StartOffset, vd.Size } };
                        }
                    }
                    break;
                }
            }
        }
    }

    // Also check VS for additional b1 variables not in PS
    {
        ComPtr<ID3D12ShaderReflection> refl;
        if (SUCCEEDED(D3DReflect(m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(),
                                 IID_PPV_ARGS(&refl)))) {
            D3D12_SHADER_DESC sd{};
            refl->GetDesc(&sd);
            for (UINT i = 0; i < sd.ConstantBuffers; ++i) {
                ID3D12ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(i);
                D3D12_SHADER_BUFFER_DESC cbd{};
                cb->GetDesc(&cbd);
                if (strcmp(cbd.Name, "ShaderConstants") == 0) {
                    if (cbd.Size > m_b1Size) m_b1Size = cbd.Size;
                    for (UINT j = 0; j < cbd.Variables; ++j) {
                        ID3D12ShaderReflectionVariable* var = cb->GetVariableByIndex(j);
                        D3D12_SHADER_VARIABLE_DESC vd{};
                        var->GetDesc(&vd);
                        if (!(vd.uFlags & D3D_SVF_USED)) continue;
                        // only add if not already present
                        bool found = false;
                        for (auto& kv : m_b1Fields)
                            if (kv.first == String(vd.Name)) { found = true; break; }
                        if (!found) {
                            auto* entry = m_b1Fields.Append();
                            if (entry)
                                *entry = { String(vd.Name), { vd.StartOffset, vd.Size } };
                        }
                    }
                    break;
                }
            }
        }
    }

    if (!CreateRootSignature()) return false;
    if (!CreateCBs())           return false;

    m_b1Dirty = true;
    return true;
}


void Shader::Destroy(void) noexcept
{
    m_psoCache.Clear();
    m_b1Fields.Clear();
    m_b1Staging.clear();
    m_b1Size = 0;
    m_activePso = nullptr;
    m_rootSignature.Reset();
    m_b0Buffer.Reset();
    m_b1Buffer.Reset();
    m_vsBlob.Reset();
    m_psBlob.Reset();
}


Shader& Shader::Copy(const Shader& other)
{
    // Shaders are not copyable in DX12 (GPU resources), just copy metadata
    m_name = other.m_name;
    m_vs   = other.m_vs;
    m_fs   = other.m_fs;
    return *this;
}


Shader& Shader::Move(Shader& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_name         = std::move(other.m_name);
        m_vs           = std::move(other.m_vs);
        m_fs           = std::move(other.m_fs);
        m_vsBlob       = std::move(other.m_vsBlob);
        m_psBlob       = std::move(other.m_psBlob);
        m_rootSignature = std::move(other.m_rootSignature);
        m_psoCache     = std::move(other.m_psoCache);
        m_b0Staging    = other.m_b0Staging;
        m_b0Buffer     = std::move(other.m_b0Buffer);
        m_b1Size       = other.m_b1Size;
        m_b1Staging    = std::move(other.m_b1Staging);
        m_b1Buffer     = std::move(other.m_b1Buffer);
        m_b1Dirty      = other.m_b1Dirty;
        m_b1Fields     = std::move(other.m_b1Fields);
        m_activePso    = other.m_activePso;
        other.m_activePso = nullptr;
    }
    return *this;
}

// =================================================================================================
// PSO creation helpers

D3D12_BLEND Shader::ToD3DBlend(GLenum gl) noexcept
{
    switch (gl) {
        case GL_ZERO:                return D3D12_BLEND_ZERO;
        case GL_ONE:                 return D3D12_BLEND_ONE;
        case GL_SRC_ALPHA:           return D3D12_BLEND_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
        case GL_SRC_COLOR:           return D3D12_BLEND_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR: return D3D12_BLEND_INV_SRC_COLOR;
        case GL_DST_ALPHA:           return D3D12_BLEND_DEST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
        case GL_DST_COLOR:           return D3D12_BLEND_DEST_COLOR;
        case GL_ONE_MINUS_DST_COLOR: return D3D12_BLEND_INV_DEST_COLOR;
        default:                     return D3D12_BLEND_ONE;
    }
}


D3D12_BLEND_OP Shader::ToD3DBlendOp(GLenum gl) noexcept
{
    switch (gl) {
        case GL_FUNC_ADD:              return D3D12_BLEND_OP_ADD;
        case GL_FUNC_SUBTRACT:         return D3D12_BLEND_OP_SUBTRACT;
        case GL_FUNC_REVERSE_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
        case GL_MIN:                   return D3D12_BLEND_OP_MIN;
        case GL_MAX:                   return D3D12_BLEND_OP_MAX;
        default:                       return D3D12_BLEND_OP_ADD;
    }
}


D3D12_COMPARISON_FUNC Shader::ToD3DCompFunc(GLenum gl) noexcept
{
    switch (gl) {
        case GL_NEVER:    return D3D12_COMPARISON_FUNC_NEVER;
        case GL_LESS:     return D3D12_COMPARISON_FUNC_LESS;
        case GL_EQUAL:    return D3D12_COMPARISON_FUNC_EQUAL;
        case GL_LEQUAL:   return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case GL_GREATER:  return D3D12_COMPARISON_FUNC_GREATER;
        case GL_NOTEQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case GL_GEQUAL:   return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case GL_ALWAYS:   return D3D12_COMPARISON_FUNC_ALWAYS;
        default:          return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    }
}


ID3D12PipelineState* Shader::GetOrCreatePSO(const RenderState& state) noexcept
{
    // Search cache
    for (auto& e : m_psoCache)
        if (e.state == state) return e.pso.Get();

    ID3D12Device* device = dx12Context.Device();
    if (!device || !m_rootSignature || !m_vsBlob || !m_psBlob) return nullptr;

    // Rasterizer
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode              = D3D12_FILL_MODE_SOLID;
    rast.CullMode              = (state.faceCulling && state.cullMode == GL_BACK)  ? D3D12_CULL_MODE_BACK
                               : (state.faceCulling && state.cullMode == GL_FRONT) ? D3D12_CULL_MODE_FRONT
                               :                                                      D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = (state.frontFace == GL_CCW) ? TRUE : FALSE;
    rast.DepthClipEnable       = TRUE;
    rast.MultisampleEnable     = state.multiSample ? TRUE : FALSE;

    // Blend
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = state.colorMask;
    if (state.blendEnable) {
        blend.RenderTarget[0].BlendEnable    = TRUE;
        blend.RenderTarget[0].SrcBlend       = ToD3DBlend(state.blendSrcRGB);
        blend.RenderTarget[0].DestBlend      = ToD3DBlend(state.blendDstRGB);
        blend.RenderTarget[0].BlendOp        = ToD3DBlendOp(state.blendOpRGB);
        blend.RenderTarget[0].SrcBlendAlpha  = ToD3DBlend(state.blendSrcAlpha);
        blend.RenderTarget[0].DestBlendAlpha = ToD3DBlend(state.blendDstAlpha);
        blend.RenderTarget[0].BlendOpAlpha   = ToD3DBlendOp(state.blendOpAlpha);
    }

    // Depth / stencil
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable    = state.depthTest  ? TRUE  : FALSE;
    ds.DepthWriteMask = state.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc      = ToD3DCompFunc(state.depthFunc);
    ds.StencilEnable  = state.stencilTest ? TRUE : FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature     = m_rootSignature.Get();
    psoDesc.VS                 = { m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize() };
    psoDesc.PS                 = { m_psBlob->GetBufferPointer(), m_psBlob->GetBufferSize() };
    psoDesc.InputLayout        = { kInputLayout, kInputLayoutCount };
    psoDesc.RasterizerState    = rast;
    psoDesc.BlendState         = blend;
    psoDesc.DepthStencilState  = ds;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets   = 1;
    psoDesc.RTVFormats[0]      = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat          = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask         = UINT_MAX;
    psoDesc.SampleDesc.Count   = 1;

    PsoEntry* entry = m_psoCache.Append();
    if (!entry) return nullptr;
    entry->state = state;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&entry->pso));
    if (FAILED(hr)) {
#ifdef _DEBUG
        fprintf(stderr, "Shader '%s': PSO creation failed (hr=0x%08X)\n",
                (const char*)m_name, (unsigned)hr);
#endif
        m_psoCache.Remove(*entry);
        return nullptr;
    }
    return entry->pso.Get();
}

// =================================================================================================

void Shader::UploadB0(void) noexcept
{
    UploadCBData(m_b0Buffer.Get(), &m_b0Staging, sizeof(FrameConstants));
}


void Shader::UploadB1(void) noexcept
{
    if (m_b1Buffer && m_b1Size > 0 && m_b1Dirty) {
        UploadCBData(m_b1Buffer.Get(), m_b1Staging.data(), m_b1Size);
        m_b1Dirty = false;
    }
}


void Shader::Enable(void)
{
    if (!IsValid()) return;

    auto* list = cmdQueue.List();
    if (!list) return;

    // Get / create PSO for current render state
    const RenderState& state = gfxStates.State();
    ID3D12PipelineState* pso = GetOrCreatePSO(state);
    if (!pso) return;

    // Upload constant buffers
    UploadB1();
    UploadB0();

    // Set pipeline state and root signature
    list->SetPipelineState(pso);
    list->SetGraphicsRootSignature(m_rootSignature.Get());

    // Root param 0: b0 (FrameConstants)
    if (m_b0Buffer)
        list->SetGraphicsRootConstantBufferView(0, m_b0Buffer->GetGPUVirtualAddress());

    // Root param 1: b1 (ShaderConstants — bind even if empty, hardware ignores it)
    if (m_b1Buffer)
        list->SetGraphicsRootConstantBufferView(1, m_b1Buffer->GetGPUVirtualAddress());

    // Root param 2: SRV descriptor table (the heap itself is set once per frame by the renderer)
    auto& srvHeap = descriptorHeaps.m_srvHeap;
    if (srvHeap.m_heap) {
        ID3D12DescriptorHeap* heaps[] = { srvHeap.m_heap.Get() };
        list->SetDescriptorHeaps(1, heaps);
        list->SetGraphicsRootDescriptorTable(2, srvHeap.m_heap->GetGPUDescriptorHandleForHeapStart());
    }

    m_activePso = pso;
}


void Shader::UpdateMatrices(void)
{
    std::memcpy(m_b0Staging.mModelView,     baseRenderer.ModelView().AsArray(),                16 * sizeof(float));
    std::memcpy(m_b0Staging.mProjection,    baseRenderer.Projection().AsArray(),               16 * sizeof(float));
    std::memcpy(m_b0Staging.mViewport,      baseRenderer.ViewportTransformation().AsArray(),   16 * sizeof(float));
    if (shadowMap.IsReady())
        std::memcpy(m_b0Staging.mLightTransform, shadowMap.GetTransformation().AsArray(),      16 * sizeof(float));
    UploadB0();
}

// =================================================================================================
// Uniform setters

bool Shader::TrySetB0Field(const char* name, const float* data) noexcept
{
    if (strcmp(name, "mModelView") == 0) {
        std::memcpy(m_b0Staging.mModelView, data, 64);
        return true;
    }
    if (strcmp(name, "mProjection") == 0) {
        std::memcpy(m_b0Staging.mProjection, data, 64);
        return true;
    }
    if (strcmp(name, "mViewport") == 0) {
        std::memcpy(m_b0Staging.mViewport, data, 64);
        return true;
    }
    if (strcmp(name, "mLightTransform") == 0) {
        std::memcpy(m_b0Staging.mLightTransform, data, 64);
        return true;
    }
    return false;
}


int Shader::SetB1Field(const char* name, const void* data, size_t size) noexcept
{
    int* pOffset = m_locations[name];
    if (!pOffset) return -1;

    if (*pOffset == std::numeric_limits<int>::min()) {
        // First access: resolve from reflection table
        int resolved = -1;
        for (auto& kv : m_b1Fields) {
            if (kv.first == String(name)) {
                resolved = int(kv.second.offset);
                break;
            }
        }
        *pOffset = resolved;
        if (resolved < 0)
            fprintf(stderr, "Shader '%s': unknown uniform '%s'\n", (const char*)m_name, name);
    }

    if (*pOffset < 0) return -1;

    uint32_t offset = uint32_t(*pOffset);
    if (offset + size > m_b1Staging.size()) return -1;
    std::memcpy(m_b1Staging.data() + offset, data, size);
    m_b1Dirty = true;
    return *pOffset;
}


int Shader::SetFloat(const char* name, float data) noexcept
{
    return SetB1Field(name, &data, sizeof(float));
}


int Shader::SetInt(const char* name, int data) noexcept
{
    return SetB1Field(name, &data, sizeof(int));
}


int Shader::SetVector2f(const char* name, const Vector2f& data) noexcept
{
    return SetB1Field(name, &data, sizeof(Vector2f));
}

int Shader::SetVector3f(const char* name, const Vector3f& data) noexcept
{
    return SetB1Field(name, &data, sizeof(Vector3f));
}

int Shader::SetVector4f(const char* name, const Vector4f& data) noexcept
{
    return SetB1Field(name, &data, sizeof(Vector4f));
}

int Shader::SetVector2i(const char* name, const Vector2i& data) noexcept
{
    return SetB1Field(name, &data, sizeof(Vector2i));
}

int Shader::SetVector3i(const char* name, const Vector3i& data) noexcept
{
    return SetB1Field(name, &data, sizeof(Vector3i));
}

int Shader::SetVector4i(const char* name, const Vector4i& data) noexcept
{
    return SetB1Field(name, &data, sizeof(Vector4i));
}


int Shader::SetMatrix4f(const char* name, const float* data, bool /*transpose*/) noexcept
{
    if (TrySetB0Field(name, data)) return 0; // b0 field — offset 0 is valid and non-error
    return SetB1Field(name, data, 16 * sizeof(float));
}


int Shader::SetMatrix3f(const char* name, float* data, bool /*transpose*/) noexcept
{
    return SetB1Field(name, data, 9 * sizeof(float));
}


int Shader::SetFloatArray(const char* name, const float* data, size_t length) noexcept
{
    return SetB1Field(name, data, length * sizeof(float));
}

int Shader::SetVector2fArray(const char* name, const Vector2f* data, int length) noexcept
{
    return SetB1Field(name, data, size_t(length) * sizeof(Vector2f));
}

int Shader::SetVector3fArray(const char* name, const Vector3f* data, int length) noexcept
{
    return SetB1Field(name, data, size_t(length) * sizeof(Vector3f));
}

int Shader::SetVector4fArray(const char* name, const Vector4f* data, int length) noexcept
{
    return SetB1Field(name, data, size_t(length) * sizeof(Vector4f));
}

// =================================================================================================
