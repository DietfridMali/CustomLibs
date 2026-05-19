#define NOMINMAX

#include <utility>
#include <cstring>
#include <vector>
#include <string>

#include "dx12framework.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxc/dxcapi.h>
#include <wrl/client.h>

#include "compute_shader.h"
#include "dx12context.h"
#include "cbv_allocator.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")

using Microsoft::WRL::ComPtr;

// =================================================================================================
// DX12 ComputeShader implementation
//
// HLSL CS source -> DXIL via DXC (target cs_6_0). Root signature is built from the binding
// descriptor list: one root CBV per UniformBuffer entry, one descriptor table per SRV/Sampler/
// UAV slot (1-entry table each, mirroring the graphics Shader's layout). Pipeline created via
// CreateComputePipelineState.
//
// The runtime calls (SetComputeRootSignature / SetPipelineState / Dispatch / table binds)
// happen in the caller — see directx/src/cloudrenderer.cpp for the TSP driver.
// =================================================================================================

namespace {

ComPtr<IDxcUtils>     g_dxcUtils;
ComPtr<IDxcCompiler3> g_dxcCompiler;
bool                  g_dxcInitialized = false;

bool InitDxc(void) noexcept {
    if (g_dxcInitialized)
        return true;
    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(g_dxcUtils.GetAddressOf()))))
        return false;
    if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(g_dxcCompiler.GetAddressOf())))) {
        g_dxcUtils.Reset();
        return false;
    }
    g_dxcInitialized = true;
    return true;
}

std::wstring ToWide(const char* utf8) noexcept {
    std::wstring s;
    if (utf8)
        while (*utf8)
            s.push_back(wchar_t(uint8_t(*utf8++)));
    return s;
}

}  // namespace


bool ComputeShader::Compile(const char* hlslCode, const char* entryPoint) noexcept
{
    if ((not hlslCode) or (not *hlslCode))
        return false;
    if (not InitDxc())
        return false;

    DxcBuffer src{};
    src.Ptr = hlslCode;
    src.Size = std::strlen(hlslCode);
    src.Encoding = DXC_CP_ACP;

    std::wstring wEntry = ToWide(entryPoint);
    std::vector<LPCWSTR> args = {
        L"-E", wEntry.c_str(),
        L"-T", L"cs_6_0",
        L"-Zpc",        // column-major matrices
#ifdef _DEBUG
        L"-Zi", L"-Od",
#else
        L"-O3",
#endif
    };

    ComPtr<IDxcResult> result;
    HRESULT hr = g_dxcCompiler->Compile(&src, args.data(), UINT32(args.size()), nullptr, IID_PPV_ARGS(result.GetAddressOf()));
    if (FAILED(hr))
        return false;

    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr);
    if (errors and errors->GetStringLength() > 0)
        fprintf(stderr, "ComputeShader '%s': %s\n", (const char*)m_name, errors->GetStringPointer());

    HRESULT status = E_FAIL;
    result->GetStatus(&status);
    if (FAILED(status))
        return false;

    ComPtr<IDxcBlob> obj;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(obj.GetAddressOf()), nullptr)) or not obj)
        return false;

    if (FAILED(D3DCreateBlob(obj->GetBufferSize(), m_csBytecode.GetAddressOf())))
        return false;
    std::memcpy(m_csBytecode->GetBufferPointer(), obj->GetBufferPointer(), obj->GetBufferSize());
    return true;
}


bool ComputeShader::CreateRootSignature(const AutoArray<ComputeBindingDesc>& bindings) noexcept
{
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    // Pass 1: bucketize bindings by kind. SPIR-V binding numbers map to HLSL registers:
    //   binding 0 → b0      binding 1 → b1
    //   binding 4..19 → t0..t15
    //   binding 20..35 → s0..s15
    //   binding 36..39 → u0..u3
    std::vector<D3D12_DESCRIPTOR_RANGE> srvRanges;
    std::vector<D3D12_DESCRIPTOR_RANGE> samplerRanges;
    std::vector<D3D12_DESCRIPTOR_RANGE> uavRanges;
    std::vector<std::pair<uint32_t, ComputeBindingDesc::Kind>> orderedBindings; // root-param order

    auto registerForBinding = [](uint32_t binding, ComputeBindingDesc::Kind kind) -> uint32_t {
        switch (kind) {
            case ComputeBindingDesc::Kind::UniformBuffer: return binding;          // b0/b1
            case ComputeBindingDesc::Kind::SampledImage:  return binding - 4u;     // t0..
            case ComputeBindingDesc::Kind::Sampler:       return binding - 20u;    // s0..
            case ComputeBindingDesc::Kind::StorageImage:  return binding - 36u;    // u0..
            default: return binding;
        }
    };

    for (int i = 0; i < bindings.Length(); ++i)
        orderedBindings.push_back({ bindings[i].binding, bindings[i].kind });

    // Allocate root parameters in the order of bindings — caller side knows the order and
    // SetComputeRootDescriptorTable(rootIdx, ...) uses these slots.
    std::vector<D3D12_ROOT_PARAMETER> params;
    params.reserve(orderedBindings.size());

    // Reserve room so the D3D12_DESCRIPTOR_RANGE pointers stay stable.
    srvRanges.reserve(16);
    samplerRanges.reserve(16);
    uavRanges.reserve(4);

    for (size_t i = 0; i < orderedBindings.size(); ++i) {
        uint32_t binding = orderedBindings[i].first;
        ComputeBindingDesc::Kind kind = orderedBindings[i].second;
        uint32_t reg = registerForBinding(binding, kind);

        D3D12_ROOT_PARAMETER p{};
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        if (kind == ComputeBindingDesc::Kind::UniformBuffer) {
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            p.Descriptor.ShaderRegister = reg;
            p.Descriptor.RegisterSpace = 0;
            if (reg < 2)
                m_cbvRootIndex[reg] = int32_t(params.size());
        }
        else {
            D3D12_DESCRIPTOR_RANGE r{};
            r.NumDescriptors = 1;
            r.BaseShaderRegister = reg;
            r.RegisterSpace = 0;
            r.OffsetInDescriptorsFromTableStart = 0;
            if (kind == ComputeBindingDesc::Kind::SampledImage) {
                r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                srvRanges.push_back(r);
                p.DescriptorTable.NumDescriptorRanges = 1;
                p.DescriptorTable.pDescriptorRanges = &srvRanges.back();
                if (reg < 16) m_srvRootIndex[reg] = int32_t(params.size());
            }
            else if (kind == ComputeBindingDesc::Kind::Sampler) {
                r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                samplerRanges.push_back(r);
                p.DescriptorTable.NumDescriptorRanges = 1;
                p.DescriptorTable.pDescriptorRanges = &samplerRanges.back();
                if (reg < 16) m_samplerRootIndex[reg] = int32_t(params.size());
            }
            else if (kind == ComputeBindingDesc::Kind::StorageImage) {
                r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                uavRanges.push_back(r);
                p.DescriptorTable.NumDescriptorRanges = 1;
                p.DescriptorTable.pDescriptorRanges = &uavRanges.back();
                if (reg < 4) m_uavRootIndex[reg] = int32_t(params.size());
            }
            else {
                continue;  // unsupported kind
            }
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        }
        params.push_back(p);
    }

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = UINT(params.size());
    rsd.pParameters = params.data();
    rsd.NumStaticSamplers = 0;
    rsd.pStaticSamplers = nullptr;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig, err;
    HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, sig.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err)
            fprintf(stderr, "ComputeShader '%s': root sig serialize: %s\n",
                    (const char*)m_name, (const char*)err->GetBufferPointer());
        return false;
    }
    return SUCCEEDED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                 IID_PPV_ARGS(m_rootSignature.GetAddressOf())));
}


bool ComputeShader::CreatePipeline(void) noexcept
{
    ID3D12Device* device = dx12Context.Device();
    if (not device or not m_rootSignature or not m_csBytecode)
        return false;

    D3D12_COMPUTE_PIPELINE_STATE_DESC psd{};
    psd.pRootSignature = m_rootSignature.Get();
    psd.CS.pShaderBytecode = m_csBytecode->GetBufferPointer();
    psd.CS.BytecodeLength = m_csBytecode->GetBufferSize();
    psd.NodeMask = 0;
    psd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    return SUCCEEDED(device->CreateComputePipelineState(&psd, IID_PPV_ARGS(m_pipeline.GetAddressOf())));
}


bool ComputeShader::Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings)
{
    if (IsValid())
        return true;
    if (not Compile((const char*)csCode, "CSMain"))
        return false;
    m_bindings = bindings;
    if (not CreateRootSignature(bindings))
        return false;
    if (not CreatePipeline())
        return false;
    ReflectB1Fields();
    if (m_b1Size > 0) {
        m_b1Staging.assign(m_b1Size, 0);
        m_b1Dirty = true;
    }
    m_cs = csCode;
    return true;
}


void ComputeShader::ReflectB1Fields(void) noexcept
{
    if (not m_csBytecode or not InitDxc())
        return;
    ComPtr<ID3D12ShaderReflection> refl;
    {
        DxcBuffer rb{};
        rb.Ptr = m_csBytecode->GetBufferPointer();
        rb.Size = m_csBytecode->GetBufferSize();
        rb.Encoding = DXC_CP_ACP;
        if (FAILED(g_dxcUtils->CreateReflection(&rb, IID_PPV_ARGS(refl.GetAddressOf()))))
            return;
    }

    D3D12_SHADER_DESC sd{};
    refl->GetDesc(&sd);
    for (UINT i = 0; i < sd.ConstantBuffers; ++i) {
        ID3D12ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(i);
        D3D12_SHADER_BUFFER_DESC cbd{};
        cb->GetDesc(&cbd);
        if (strcmp(cbd.Name, "ShaderConstants") != 0)
            continue;
        if (cbd.Size > m_b1Size)
            m_b1Size = cbd.Size;
        for (UINT j = 0; j < cbd.Variables; ++j) {
            ID3D12ShaderReflectionVariable* var = cb->GetVariableByIndex(j);
            D3D12_SHADER_VARIABLE_DESC vd{};
            var->GetDesc(&vd);
            if (not (vd.uFlags & D3D_SVF_USED))
                continue;
            bool found = false;
            for (auto& kv : m_b1Fields)
                if (kv.first == String(vd.Name)) {
                    found = true;
                    break;
                }
            if (not found) {
                auto* entry = m_b1Fields.Append();
                if (entry)
                    *entry = { String(vd.Name), { vd.StartOffset, vd.Size } };
            }
        }
        break;
    }
}


void ComputeShader::Destroy(void) noexcept
{
    m_pipeline.Reset();
    m_rootSignature.Reset();
    m_csBytecode.Reset();
    m_b1Staging.clear();
    m_b1Fields.Reset();
    m_b1Size = 0;
    m_b1Dirty = true;
    m_b1GpuVA = 0;
    for (int i = 0; i < 2; ++i) m_cbvRootIndex[i] = -1;
    for (int i = 0; i < 16; ++i) { m_srvRootIndex[i] = -1; m_samplerRootIndex[i] = -1; }
    for (int i = 0; i < 4; ++i) m_uavRootIndex[i] = -1;
}


bool ComputeShader::Activate(void) {
    return IsValid();
}


bool ComputeShader::Dispatch(uint32_t /*x*/, uint32_t /*y*/, uint32_t /*z*/) {
    return IsValid();
}


bool ComputeShader::Dispatch2D(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY) {
    if (tileX == 0 or tileY == 0) return false;
    return Dispatch((width + tileX - 1) / tileX, (height + tileY - 1) / tileY, 1);
}


int ComputeShader::SetB1(uint32_t offset, const void* data, size_t size) noexcept {
    if (offset + size > m_b1Staging.size())
        m_b1Staging.resize(offset + size, 0);
    std::memcpy(m_b1Staging.data() + offset, data, size);
    m_b1Dirty = true;
    return int(offset);
}


int ComputeShader::SetB1Field(const char* name, const void* data, size_t size) noexcept
{
    String sName(name);
    for (auto& kv : m_b1Fields) {
        if (kv.first == sName) {
            uint32_t offset = kv.second.offset;
            if (offset + size <= m_b1Staging.size()) {
                std::memcpy(m_b1Staging.data() + offset, data, size);
                m_b1Dirty = true;
                return int(offset);
            }
            return -1;
        }
    }
#ifdef _DEBUG
    fprintf(stderr, "ComputeShader '%s': unknown uniform '%s'\n", (const char*)m_name, name);
#endif
    return -1;
}


int ComputeShader::SetFloat   (const char* name, float data)            noexcept { return SetB1Field(name, &data, sizeof(float)); }
int ComputeShader::SetInt     (const char* name, int data)              noexcept { return SetB1Field(name, &data, sizeof(int)); }
int ComputeShader::SetVector2f(const char* name, const Vector2f& data)  noexcept { return SetB1Field(name, &data, sizeof(Vector2f)); }
int ComputeShader::SetVector3f(const char* name, const Vector3f& data)  noexcept { return SetB1Field(name, &data, sizeof(Vector3f)); }
int ComputeShader::SetVector4f(const char* name, const Vector4f& data)  noexcept { return SetB1Field(name, &data, sizeof(Vector4f)); }
int ComputeShader::SetVector2i(const char* name, const Vector2i& data)  noexcept { return SetB1Field(name, &data, sizeof(Vector2i)); }
int ComputeShader::SetVector3i(const char* name, const Vector3i& data)  noexcept { return SetB1Field(name, &data, sizeof(Vector3i)); }
int ComputeShader::SetVector4i(const char* name, const Vector4i& data)  noexcept { return SetB1Field(name, &data, sizeof(Vector4i)); }
int ComputeShader::SetMatrix4f(const char* name, const float* data, bool /*transpose*/) noexcept { return SetB1Field(name, data, 16 * sizeof(float)); }
int ComputeShader::SetMatrix3f(const char* name, const float* data, bool /*transpose*/) noexcept { return SetB1Field(name, data, 9 * sizeof(float)); }


bool ComputeShader::UploadB1(void) noexcept
{
    if (m_b1Size == 0)
        return true;
    CbAlloc a = cbvAllocator.Allocate(UINT(m_b1Size));
    if (not a.IsValid())
        return false;
    std::memcpy(a.cpu, m_b1Staging.data(), m_b1Size);
    m_b1GpuVA = a.gpu;
    m_b1Dirty = false;
    return true;
}

// =================================================================================================
