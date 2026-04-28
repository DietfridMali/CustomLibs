#define NOMINMAX

#include <utility>
#include <cstring>
#include <ranges>
#include <string_view>

#include "dx12framework.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>

#include "shader.h"
#include "cbv_allocator.h"
#include "shadowmap.h"
#include "base_renderer.h"
#include "commandlist.h"
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
// PSO cache: keyed by RenderStates bitmask; created on first Enable() for that state.

// -------------------------------------------------------------------------------------------------
// Helpers to translate ShaderDataAttributes to D3D12_INPUT_ELEMENT_DESC.

// Maps C++ buffer type + id to the DX12 input slot used by GfxDataLayout::FixedSlotForBuffer.
//   slot 0: Vertex, slot 1-3: TexCoord/0-2, slot 4: Color,
//   slot 5: Normal, slot 6: Tangent, slot 7+: Offset/Float
static int SlotForAttr(const char* datatype, int id) noexcept
{
    if (strcmp(datatype, "Vertex") == 0)
        return 0;
    if (strcmp(datatype, "TexCoord") == 0) {
        if (id >= 0 and id <= 2)
            return 1 + id;
        return -1;
    }
    if (strcmp(datatype, "Color") == 0)
        return 4;
    if (strcmp(datatype, "Normal") == 0)
        return 5;
    if (strcmp(datatype, "Tangent") == 0)
        return 6;
    if (strcmp(datatype, "Offset") == 0)
        return 7 + id;
    if (strcmp(datatype, "Float") == 0)
        return 7 + id;
    return -1;
}

// Maps C++ buffer type + id to an HLSL semantic name and index.
// Offset/N uses TEXCOORD(N+3) to avoid collisions with TexCoord/0..2.
static const char* SemanticForAttr(const char* datatype, int id, UINT& semanticIndex) noexcept
{
    if (strcmp(datatype, "Vertex") == 0)   { 
        semanticIndex = 0;        
        return "POSITION"; 
    }
    if (strcmp(datatype, "TexCoord") == 0) { 
        semanticIndex = UINT(id); 
        return "TEXCOORD"; 
    }
    if (strcmp(datatype, "Color") == 0)    { 
        semanticIndex = UINT(id); 
        return "COLOR"; 
    }
    if (strcmp(datatype, "Normal") == 0)   { 
        semanticIndex = UINT(id); 
        return "NORMAL"; 
    }
    if (strcmp(datatype, "Tangent") == 0)  { 
        semanticIndex = UINT(id); 
        return "TANGENT"; 
    }
    if (strcmp(datatype, "Offset") == 0)   {
        semanticIndex = UINT(id);
        return "OFFSET";
    }
    if (strcmp(datatype, "Float") == 0)   {
        semanticIndex = UINT(id);
        return "FLOAT";
    }
    semanticIndex = 0;
    return "TEXCOORD";
}

static DXGI_FORMAT DxgiFormatForAttr(ShaderDataAttributes::Format fmt) noexcept
{
    switch (fmt) {
    case ShaderDataAttributes::Float1: 
        return DXGI_FORMAT_R32_FLOAT;
    case ShaderDataAttributes::Float2: 
        return DXGI_FORMAT_R32G32_FLOAT;
    case ShaderDataAttributes::Float3: 
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case ShaderDataAttributes::Float4: 
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    return DXGI_FORMAT_R32G32B32_FLOAT;
}


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
    HRESULT hr = D3DCompile(hlslCode, strlen(hlslCode), (const char*)m_name, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, flags, 0, &blobOut, &errBlob);
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
        if (not line.empty() and line.back() == '\r')
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

    // Params kSrvBase..kSrvBase+15: one 1-entry descriptor table per texture slot (t0..t15).
    // Each slot is bound independently in Texture::Bind(tmuIndex) via
    // SetGraphicsRootDescriptorTable(kSrvBase + tmuIndex, ...).
    D3D12_DESCRIPTOR_RANGE srvRanges[kSrvSlots]{};
    for (int i = 0; i < kSrvSlots; ++i) {
        srvRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[i].NumDescriptors = 1;
        srvRanges[i].BaseShaderRegister = UINT(i); // t0, t1, ...
        srvRanges[i].RegisterSpace = 0;
        srvRanges[i].OffsetInDescriptorsFromTableStart = 0;
    }

    // Params kUavBase..kUavBase+3: one 1-entry descriptor table per UAV slot (u0..u3).
    static constexpr int kUavSlots = 4;
    D3D12_DESCRIPTOR_RANGE uavRanges[kUavSlots]{};
    for (int i = 0; i < kUavSlots; ++i) {
        uavRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRanges[i].NumDescriptors = 1;
        uavRanges[i].BaseShaderRegister = UINT(i); // u0, u1, ...
        uavRanges[i].RegisterSpace = 0;
        uavRanges[i].OffsetInDescriptorsFromTableStart = 0;
    }

    D3D12_ROOT_PARAMETER params[kUavBase + kUavSlots]{};

    // Root CBV b0 — FrameConstants (visible to all stages)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root CBV b1 — VS ShaderConstants (VERTEX only)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // Root CBV b1 — PS ShaderConstants (PIXEL only)
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 1;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Root CBV b1 — GS ShaderConstants (GEOMETRY only)
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].Descriptor.ShaderRegister = 1;
    params[3].Descriptor.RegisterSpace = 0;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;

    // One 1-entry descriptor table per SRV slot (t0..t15)
    for (int i = 0; i < kSrvSlots; ++i) {
        params[kSrvBase + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kSrvBase + i].DescriptorTable.NumDescriptorRanges = 1;
        params[kSrvBase + i].DescriptorTable.pDescriptorRanges = &srvRanges[i];
        params[kSrvBase + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    // One 1-entry descriptor table per UAV slot (u0..u3)
    for (int i = 0; i < kUavSlots; ++i) {
        params[kUavBase + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kUavBase + i].DescriptorTable.NumDescriptorRanges = 1;
        params[kUavBase + i].DescriptorTable.pDescriptorRanges = &uavRanges[i];
        params[kUavBase + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
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
    rsd.NumParameters = kUavBase + kUavSlots;
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




void Shader::BuildInputLayout(void) noexcept
{
    if (m_dataLayout.m_count > 0) {
        for (int i = 0; i < m_dataLayout.m_count; ++i) {
            const ShaderDataAttributes& attr = m_dataLayout.m_attrs[i];
            UINT semanticIndex = 0;
            const char* semantic = SemanticForAttr(attr.datatype, attr.id, semanticIndex);
            int slot = SlotForAttr(attr.datatype, attr.id);
            if (slot < 0)
                continue;
            D3D12_INPUT_ELEMENT_DESC desc{};
            desc.SemanticName = semantic;
            desc.SemanticIndex = semanticIndex;
            desc.Format = DxgiFormatForAttr(attr.format);
            desc.InputSlot = UINT(slot);
            desc.AlignedByteOffset = 0;
            desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            desc.InstanceDataStepRate = 0;
            m_vsInputLayout.push_back(desc);
        }
    }
    else {
        static const D3D12_INPUT_ELEMENT_DESC kFallbackLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,     0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,        2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,  3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  4, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT,  5, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,  6, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        static constexpr UINT kFallbackCount = UINT(std::size(kFallbackLayout));
        ComPtr<ID3D12ShaderReflection> vsRefl;
        if (SUCCEEDED(D3DReflect(m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(), IID_PPV_ARGS(&vsRefl)))) {
            D3D12_SHADER_DESC sd{};
            vsRefl->GetDesc(&sd);
            for (UINT i = 0; i < sd.InputParameters; ++i) {
                D3D12_SIGNATURE_PARAMETER_DESC pd{};
                vsRefl->GetInputParameterDesc(i, &pd);
                if (pd.SystemValueType != D3D_NAME_UNDEFINED)
                    continue;
                for (UINT j = 0; j < kFallbackCount; ++j) {
                    if ((_stricmp(kFallbackLayout[j].SemanticName, pd.SemanticName) == 0) and (kFallbackLayout[j].SemanticIndex == pd.SemanticIndex)) {
                        m_vsInputLayout.push_back(kFallbackLayout[j]);
                        break;
                    }
                }
            }
        }
        if (m_vsInputLayout.empty())
            m_vsInputLayout.assign(kFallbackLayout, kFallbackLayout + kFallbackCount);
    }
}


bool Shader::Create(const String& vsCode, const String& fsCode, const String& gsCode)
{
    if (IsValid())
        return true;

    if (not Compile((const char*)vsCode, "VSMain", "vs_5_1", m_vsBlob))
        return false;
    if (not Compile((const char*)fsCode, "PSMain", "ps_5_1", m_psBlob))
        return false;
    if (gsCode.Length() > 0)
        Compile((const char*)gsCode, "GSMain", "gs_5_1", m_gsBlob);  // optional — failure is non-fatal

    BuildInputLayout();

    UpdateStageFields(m_vsBlob.Get(), kStageVS);
    UpdateStageFields(m_psBlob.Get(), kStagePS);
    if (m_gsBlob)
        UpdateStageFields(m_gsBlob.Get(), kStageGS);

    if (not CreateRootSignature())
        return false;

    for (int s = 0; s < kStageCount; ++s) {
        if (m_stages[s].size > 0)
            m_stages[s].staging.assign(m_stages[s].size, 0);
        m_stages[s].dirty = true;
    }
    return true;
}


void Shader::UpdateStageFields(ID3DBlob* blob, int stage) noexcept
{
    if (not blob)
        return;
    ComPtr<ID3D12ShaderReflection> refl;
    if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&refl))))
        return;
    StageConstants& sc = m_stages[stage];
    D3D12_SHADER_DESC sd{};
    refl->GetDesc(&sd);
    for (UINT i = 0; i < sd.ConstantBuffers; ++i) {
        ID3D12ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(i);
        D3D12_SHADER_BUFFER_DESC cbd{};
        cb->GetDesc(&cbd);
        if (strcmp(cbd.Name, "ShaderConstants") == 0) {
            if (cbd.Size > sc.size)
                sc.size = cbd.Size;
            for (UINT j = 0; j < cbd.Variables; ++j) {
                ID3D12ShaderReflectionVariable* var = cb->GetVariableByIndex(j);
                D3D12_SHADER_VARIABLE_DESC vd{};
                var->GetDesc(&vd);
                if (not (vd.uFlags & D3D_SVF_USED))
                    continue;
                bool found = false;
                for (auto& kv : sc.fields)
                    if (kv.first == String(vd.Name)) {
                        found = true;
                        break;
                    }
                if (not found) {
                    auto* entry = sc.fields.Append();
                    if (entry)
                        *entry = { String(vd.Name), { vd.StartOffset, vd.Size } };
                }
            }
            break;
        }
    }
}


void Shader::Destroy(void) noexcept
{
    PSO::RemovePSOs(this);
    for (int s = 0; s < kStageCount; ++s) {
        m_stages[s].fields.Clear();
        m_stages[s].staging.clear();
        m_stages[s].size = 0;
        m_stages[s].dirty = true;
    }
    m_vsInputLayout.clear();
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
        m_b0Staging    = other.m_b0Staging;
        for (int s = 0; s < kStageCount; ++s) {
            m_stages[s].size    = other.m_stages[s].size;
            m_stages[s].staging = std::move(other.m_stages[s].staging);
            m_stages[s].dirty   = other.m_stages[s].dirty;
            m_stages[s].fields  = std::move(other.m_stages[s].fields);
        }
        m_vsInputLayout = std::move(other.m_vsInputLayout);
    }
    return *this;
}

// =================================================================================================

bool Shader::UploadB1(void) noexcept
{
    auto* list = commandListHandler.CurrentGfxList();
    if (not list)
        return false;

    for (int s = 0; s < kStageCount; ++s) {
        StageConstants& sc = m_stages[s];
        if (sc.size == 0)
            continue;
        CbAlloc a = cbvAllocator.Allocate(sc.size);
        if (not a.IsValid())
            return false;
        std::memcpy(a.cpu, sc.staging.data(), sc.size);
        list->SetGraphicsRootConstantBufferView(UINT(1 + s), a.gpu);
        sc.dirty = false;
    }
    return true;
}


bool Shader::Activate(void) {
    if (not IsValid())
        return false;

    auto* list = commandListHandler.CurrentGfxList();
    if (not list)
        return false;

    CommandList* cl = commandListHandler.CurrentCmdList();
    if (not cl)
        return false;

    ID3D12PipelineState* pso = cl->GetPSO(this);
    if (not pso) {
#ifdef _DEBUG
        pso = cl->GetPSO(this);
#endif
        return false;
    }

    list->OMSetStencilRef(baseRenderer.RenderStates().stencilRef);

    auto& srvHeap = descriptorHeaps.m_srvHeap;
    if (srvHeap.m_heap) {
        ID3D12DescriptorHeap* heaps[] = { srvHeap.m_heap.Get() };
        list->SetDescriptorHeaps(1, heaps);
    }
    return true;
}


bool Shader::UpdateMatrices(void)
{
    auto* list = commandListHandler.CurrentGfxList();
    if (not list)
        return false;

    std::memcpy(m_b0Staging.mModelView, baseRenderer.ModelView().AsArray(), 16 * sizeof(float));
    std::memcpy(m_b0Staging.mProjection, baseRenderer.Projection().AsArray(), 16 * sizeof(float));
    std::memcpy(m_b0Staging.mViewport, baseRenderer.ViewportTransformation().AsArray(), 16 * sizeof(float));
    if (shadowMap.IsReady())
        std::memcpy(m_b0Staging.mLightTransform, shadowMap.GetTransformation().AsArray(), 16 * sizeof(float));

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
    int result = -1;
    String sName(name);
    for (int s = 0; s < kStageCount; ++s) {
        StageConstants& sc = m_stages[s];
        if (sc.size == 0)
            continue;
        for (auto& kv : sc.fields) {
            if (kv.first == sName) {
                uint32_t offset = kv.second.offset;
                if (offset + size <= sc.staging.size()) {
                    std::memcpy(sc.staging.data() + offset, data, size);
                    sc.dirty = true;
                    if (result < 0)
                        result = int(offset);
                }
                break;
            }
        }
    }
    if (result < 0) {
        int* pOffset = m_locations[name];
        if (pOffset and *pOffset == std::numeric_limits<int>::min()) {
            *pOffset = -1;
            fprintf(stderr, "Shader '%s': unknown uniform '%s'\n", (const char*)m_name, name);
        }
    }
    return result;
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
