#define NOMINMAX

#include <utility>
#include <cstring>
#include <ranges>
#include <string_view>

#include "framework.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>

#include "shader.h"
#include "cbv_allocator.h"
#include "shadowmap.h"
#include "base_renderer.h"
#include "command_queue.h"
#include "descriptor_heap.h"
#include "dx12context.h"
#include "gfxdriverstates.h"

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

// Default input layout matching mesh.h UpdateVAO() stream order:
//   slot 0: POSITION  float3  - vertex positions
//   slot 1: TEXCOORD0 float2  - primary texcoord (texCoord[0])
//   slot 2: TEXCOORD1 float2  - secondary texcoord (texCoord[1] / borderTexCoord)
//   slot 3: COLOR0    float4  - colour / wallScale (use .xy for vec2)
//   slot 4: NORMAL    float4  - normal / decalNormal  (float4 to cover both vec3+padding and vec4)
//   slot 5: TANGENT   float4  - tangent (with handedness in .w)
//   slot 6: TEXCOORD2 float4  - extra per-vertex data (morph-target offsets, etc.)
//
// NOTE: meshes that supply a float3 normal at slot 4 must pad to float4 (w = 0).
// Sphere/smiley meshes that put normals at slot 1 (layout(location=1) in the old GLSL)
// should instead write a dummy float2 at slot 1 and the normal at slot 4.
static const D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 5, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 6, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};
static constexpr UINT kInputLayoutCount = UINT(std::size(kInputLayout));


// =================================================================================================

bool Shader::Compile(const char* hlslCode, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& blobOut) noexcept
{
    if (not hlslCode or not *hlslCode) 
        return false;

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
        PrintShaderSource(hlslCode, target);
#endif
        return false;
    }
    return true;
}


#ifdef _DEBUG
void Shader::PrintShaderSource(const char* hlslCode, const char* title) noexcept
{
    if (not hlslCode or not *hlslCode) 
        return;
    const std::string_view src(hlslCode);
    const size_t lineCount = std::ranges::count(src, '\n') + 1;
    const int width = static_cast<int>(std::to_string(lineCount).size());
    fprintf(stderr, "\n%s\n", title);
    int lineNo = 0;
    for (auto&& chunk : src | std::views::split('\n')) {
        std::string_view line(chunk.begin(), chunk.end());
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        fprintf(stderr, "%*d: %.*s\n", width, ++lineNo,
                static_cast<int>(line.size()), line.data());
    }
    fprintf(stderr, "\n");
}
#endif


bool Shader::CreateRootSignature(void) noexcept
{
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    // Params 2..17: one 1-entry descriptor table per texture slot (t0..t15).
    // Each slot is bound independently in Texture::Bind(tmuIndex) via
    // SetGraphicsRootDescriptorTable(2 + tmuIndex, ...), so textures can live
    // at arbitrary (non-consecutive) heap slots.
    static constexpr int kSrvSlots = 16;
    D3D12_DESCRIPTOR_RANGE srvRanges[kSrvSlots]{};
    for (int i = 0; i < kSrvSlots; ++i) {
        srvRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[i].NumDescriptors = 1;
        srvRanges[i].BaseShaderRegister = UINT(i); // t0, t1, ...
        srvRanges[i].RegisterSpace = 0;
        srvRanges[i].OffsetInDescriptorsFromTableStart = 0;
    }

    D3D12_ROOT_PARAMETER params[2 + kSrvSlots]{};

    // Root CBV b0 — FrameConstants
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root CBV b1 — ShaderConstants
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // One 1-entry descriptor table per SRV slot (t0..t15)
    for (int i = 0; i < kSrvSlots; ++i) {
        params[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2 + i].DescriptorTable.NumDescriptorRanges = 1;
        params[2 + i].DescriptorTable.pDescriptorRanges = &srvRanges[i];
        params[2 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    // Static samplers: s0 = linear clamp, s1 = linear repeat
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (int i = 0; i < 2; ++i) {
        samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[i].AddressU =
        samplers[i].AddressV =
        samplers[i].AddressW = (i == 0) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
                                        : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].MaxAnisotropy = 1;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[i].MinLOD = 0.0f;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = UINT(i);
        samplers[i].RegisterSpace = 0;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2 + kSrvSlots;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers = samplers;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
              | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
              | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;

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




bool Shader::Create(const String& vsCode, const String& fsCode, const String& gsCode)
{
    Destroy();

    if (!Compile((const char*)vsCode, "VSMain", "vs_5_1", m_vsBlob)) return false;
    if (!Compile((const char*)fsCode, "PSMain", "ps_5_1", m_psBlob)) return false;
    if (gsCode.Length() > 0)
        Compile((const char*)gsCode, "GSMain", "gs_5_1", m_gsBlob);  // optional — failure is non-fatal

    // Reflect VS inputs → build a minimal per-shader input layout.
    // Only include kInputLayout entries that the VS actually reads so that
    // unused slots don't require bound vertex buffers at draw time.
    {
        ComPtr<ID3D12ShaderReflection> vsRefl;
        if (SUCCEEDED(D3DReflect(m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(),
                                 IID_PPV_ARGS(&vsRefl)))) {
            D3D12_SHADER_DESC sd{};
            vsRefl->GetDesc(&sd);
            for (UINT i = 0; i < sd.InputParameters; ++i) {
                D3D12_SIGNATURE_PARAMETER_DESC pd{};
                vsRefl->GetInputParameterDesc(i, &pd);
                if (pd.SystemValueType != D3D_NAME_UNDEFINED) continue; // skip SV_* semantics
                for (UINT j = 0; j < kInputLayoutCount; ++j) {
                    if (_stricmp(kInputLayout[j].SemanticName, pd.SemanticName) == 0 &&
                        kInputLayout[j].SemanticIndex == pd.SemanticIndex) {
                        m_vsInputLayout.push_back(kInputLayout[j]);
                        break;
                    }
                }
            }
        }
        if (m_vsInputLayout.empty()) // fallback: reflection failed
            m_vsInputLayout.assign(kInputLayout, kInputLayout + kInputLayoutCount);
    }

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

    // Also check GS for additional b1 variables
    if (m_gsBlob) {
        ComPtr<ID3D12ShaderReflection> refl;
        if (SUCCEEDED(D3DReflect(m_gsBlob->GetBufferPointer(), m_gsBlob->GetBufferSize(),
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

    if (!CreateRootSignature())
        return false;

    if (m_b1Size > 0)
        m_b1Staging.assign(m_b1Size, 0);

    m_b1Dirty = true;
    return true;
}


void Shader::Destroy(void) noexcept
{
    m_psoCache.Clear();
    m_b1Fields.Clear();
    m_b1Staging.clear();
    m_vsInputLayout.clear();
    m_b1Size = 0;
    m_activePso = nullptr;
    m_rootSignature.Reset();
    m_vsBlob.Reset();
    m_psBlob.Reset();
    m_gsBlob.Reset();
}


Shader& Shader::Copy(const Shader& other)
{
    // Shaders are not copyable in DX12 (GPU resources), just copy metadata
    m_name = other.m_name;
    m_vs   = other.m_vs;
    m_fs   = other.m_fs;
    m_gs   = other.m_gs;
    return *this;
}


Shader& Shader::Move(Shader& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_name         = std::move(other.m_name);
        m_vs           = std::move(other.m_vs);
        m_fs           = std::move(other.m_fs);
        m_gs           = std::move(other.m_gs);
        m_vsBlob       = std::move(other.m_vsBlob);
        m_psBlob       = std::move(other.m_psBlob);
        m_gsBlob       = std::move(other.m_gsBlob);
        m_rootSignature = std::move(other.m_rootSignature);
        m_psoCache     = std::move(other.m_psoCache);
        m_b0Staging    = other.m_b0Staging;
        m_b1Size       = other.m_b1Size;
        m_b1Staging    = std::move(other.m_b1Staging);
        m_b1Dirty      = other.m_b1Dirty;
        m_b1Fields     = std::move(other.m_b1Fields);
        m_vsInputLayout = std::move(other.m_vsInputLayout);
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


D3D12_STENCIL_OP Shader::ToD3DStencilOp(GLenum gl) noexcept
{
    switch (gl) {
        case GL_ZERO:      return D3D12_STENCIL_OP_ZERO;
        case GL_REPLACE:   return D3D12_STENCIL_OP_REPLACE;
        case GL_INCR:      return D3D12_STENCIL_OP_INCR_SAT;
        case GL_DECR:      return D3D12_STENCIL_OP_DECR_SAT;
        case GL_INCR_WRAP: return D3D12_STENCIL_OP_INCR;
        case GL_DECR_WRAP: return D3D12_STENCIL_OP_DECR;
        default:           return D3D12_STENCIL_OP_KEEP;
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
        if (e.state == state) 
            return e.pso.Get();

    ID3D12Device* device = dx12Context.Device();
    if (not device or not m_rootSignature or not m_vsBlob or not m_psBlob) 
        return nullptr;

    // Rasterizer
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = (state.cullMode == GL_BACK)
        ? D3D12_CULL_MODE_BACK
        : (state.cullMode == GL_FRONT) 
            ? D3D12_CULL_MODE_FRONT
            : D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = (state.frontFace == GL_CCW) ? TRUE : FALSE;
    rast.DepthClipEnable = TRUE;
    rast.MultisampleEnable = FALSE;

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
    ds.DepthEnable = state.depthTest  ? TRUE  : FALSE;
    ds.DepthWriteMask = state.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = ToD3DCompFunc(state.depthFunc);
    ds.StencilEnable = state.stencilTest ? TRUE : FALSE;
    ds.StencilReadMask = state.stencilMask;
    ds.StencilWriteMask = state.stencilMask;
    ds.FrontFace = { ToD3DStencilOp(state.stencilSFail),
                     ToD3DStencilOp(state.stencilDPFail),
                     ToD3DStencilOp(state.stencilDPPass),
                     ToD3DCompFunc(state.stencilFunc) };
    ds.BackFace  = { ToD3DStencilOp(state.stencilBackSFail),
                     ToD3DStencilOp(state.stencilBackDPFail),
                     ToD3DStencilOp(state.stencilBackDPPass),
                     ToD3DCompFunc(state.stencilFunc) };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature     = m_rootSignature.Get();
    psoDesc.VS                 = { m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize() };
    psoDesc.PS                 = { m_psBlob->GetBufferPointer(), m_psBlob->GetBufferSize() };
    if (m_gsBlob)
        psoDesc.GS             = { m_gsBlob->GetBufferPointer(), m_gsBlob->GetBufferSize() };
    psoDesc.InputLayout        = { m_vsInputLayout.data(), UINT(m_vsInputLayout.size()) };
    psoDesc.RasterizerState    = rast;
    psoDesc.BlendState         = blend;
    psoDesc.DepthStencilState  = ds;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets   = 1;
    psoDesc.RTVFormats[0]      = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat          = (ds.DepthEnable || ds.StencilEnable)
                               ? DXGI_FORMAT_D24_UNORM_S8_UINT
                               : DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleMask         = UINT_MAX;
    psoDesc.SampleDesc.Count   = 1;

    ComPtr<ID3D12PipelineState> newPso;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&newPso));
    if (FAILED(hr)) {
#ifdef _DEBUG
        fprintf(stderr, "Shader '%s': PSO creation failed (hr=0x%08X)\n",
                (const char*)m_name, (unsigned)hr);
#endif
        return nullptr;
    }
    PsoEntry* entry = m_psoCache.Append();
    if (not entry) 
        return nullptr;
    entry->state = state;
    entry->pso = std::move(newPso);
    return entry->pso.Get();
}

// =================================================================================================



bool Shader::UploadB1(void) noexcept
{
    auto* list = cmdQueue.List();
    if (not list)
        return false;

    const UINT size = (m_b1Size > 0) ? m_b1Size : 1u;  // minimum 1 byte → rounds to 256
    CbAlloc a = cbvAllocator.Allocate(size);
    if (not a.IsValid())
        return false;

    if (m_b1Size > 0)
        std::memcpy(a.cpu, m_b1Staging.data(), m_b1Size);

    list->SetGraphicsRootConstantBufferView(1, a.gpu);
    m_b1Dirty = false;
    return true;
}


void Shader::Enable(void)
{
    if (not IsValid()) 
        return;

    auto* list = cmdQueue.List();
    if (not list) 
        return;

    // Get / create PSO for current render state
    const RenderState& state = gfxDriverStates.State();
    ID3D12PipelineState* pso = GetOrCreatePSO(state);
    if (!pso) return;

    // Set pipeline state and root signature.
    // Root CBVs (b0, b1) are bound per-draw in UpdateMatrices() and UploadB1().
    list->SetPipelineState(pso);
    list->SetGraphicsRootSignature(m_rootSignature.Get());
    list->OMSetStencilRef(state.stencilRef);

    // Root params 2..17: per-slot SRV descriptor tables — set in Texture::Bind(tmuIndex)
    // via SetGraphicsRootDescriptorTable(2 + tmuIndex, heap.GpuHandle(m_handle)).
    // Prime SetDescriptorHeaps here so it's done once per shader activation.
    auto& srvHeap = descriptorHeaps.m_srvHeap;
    if (srvHeap.m_heap) {
        ID3D12DescriptorHeap* heaps[] = { srvHeap.m_heap.Get() };
        list->SetDescriptorHeaps(1, heaps);
    }

    m_activePso = pso;
}


bool Shader::UpdateMatrices(void)
{
    auto* list = cmdQueue.List();
    if (not list)
        return false;

    std::memcpy(m_b0Staging.mModelView,      baseRenderer.ModelView().AsArray(),              16 * sizeof(float));
    std::memcpy(m_b0Staging.mProjection,     baseRenderer.Projection().AsArray(),             16 * sizeof(float));
    std::memcpy(m_b0Staging.mViewport,       baseRenderer.ViewportTransformation().AsArray(), 16 * sizeof(float));
    if (shadowMap.IsReady())
        std::memcpy(m_b0Staging.mLightTransform, shadowMap.GetTransformation().AsArray(),     16 * sizeof(float));

    CbAlloc a = cbvAllocator.Allocate(sizeof(FrameConstants));
    if (not a.IsValid())
        return false;

    std::memcpy(a.cpu, &m_b0Staging, sizeof(FrameConstants));
    list->SetGraphicsRootConstantBufferView(0, a.gpu);
    return true;
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
