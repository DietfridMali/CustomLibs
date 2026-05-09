#define NOMINMAX

#include <utility>
#include <cstring>
#include <limits>

#include "vkframework.h"
#include "shader.h"
#include "shader_compiler.h"
#include "pipeline_cache.h"
#include "cbv_allocator.h"
#include "vkcontext.h"
#include "shadowmap.h"
#include "gfxstates.h"
#include "commandlist.h"
#include "descriptor_pool_handler.h"
#include "gfxrenderer.h"
#include <spirv_reflect.h>

// =================================================================================================
// Vulkan Shader implementation
//
// Descriptor-set layout (single set, all bindings — see shader.h for the full table).
// DXC compile uses -fvk-bind-register for every resource class, because b1 needs a
// stage-specific Vulkan binding (1/2/3 for VS/PS/GS) and DXC rejects mixing
// -fvk-bind-register with the -fvk-{b,t,s,u}-shift options. Bindings match the layout
// in CreatePipelineLayout: b0=0, b1=1/2/3, t0..t15=4..19, s0..s15=20..35, u0..u3=36..39.

// =================================================================================================
// Compile (HLSL -> SPIR-V via DXC)

namespace {

// Stage-independent bindings: b0, t0..t15, s0..s15, u0..u3.
// b1 is stage-specific and appended in StageArgs().
static const wchar_t* const kCommonBindArgs[] = {
    L"-fvk-bind-register", L"b0", L"0", L"0", L"0",
    L"-fvk-bind-register", L"t0", L"0", L"4", L"0",
    L"-fvk-bind-register", L"t1", L"0", L"5", L"0",
    L"-fvk-bind-register", L"t2", L"0", L"6", L"0",
    L"-fvk-bind-register", L"t3", L"0", L"7", L"0",
    L"-fvk-bind-register", L"t4", L"0", L"8", L"0",
    L"-fvk-bind-register", L"t5", L"0", L"9", L"0",
    L"-fvk-bind-register", L"t6", L"0", L"10", L"0",
    L"-fvk-bind-register", L"t7", L"0", L"11", L"0",
    L"-fvk-bind-register", L"t8", L"0", L"12", L"0",
    L"-fvk-bind-register", L"t9", L"0", L"13", L"0",
    L"-fvk-bind-register", L"t10", L"0", L"14", L"0",
    L"-fvk-bind-register", L"t11", L"0", L"15", L"0",
    L"-fvk-bind-register", L"t12", L"0", L"16", L"0",
    L"-fvk-bind-register", L"t13", L"0", L"17", L"0",
    L"-fvk-bind-register", L"t14", L"0", L"18", L"0",
    L"-fvk-bind-register", L"t15", L"0", L"19", L"0",
    L"-fvk-bind-register", L"s0", L"0", L"20", L"0",
    L"-fvk-bind-register", L"s1", L"0", L"21", L"0",
    L"-fvk-bind-register", L"s2", L"0", L"22", L"0",
    L"-fvk-bind-register", L"s3", L"0", L"23", L"0",
    L"-fvk-bind-register", L"s4", L"0", L"24", L"0",
    L"-fvk-bind-register", L"s5", L"0", L"25", L"0",
    L"-fvk-bind-register", L"s6", L"0", L"26", L"0",
    L"-fvk-bind-register", L"s7", L"0", L"27", L"0",
    L"-fvk-bind-register", L"s8", L"0", L"28", L"0",
    L"-fvk-bind-register", L"s9", L"0", L"29", L"0",
    L"-fvk-bind-register", L"s10", L"0", L"30", L"0",
    L"-fvk-bind-register", L"s11", L"0", L"31", L"0",
    L"-fvk-bind-register", L"s12", L"0", L"32", L"0",
    L"-fvk-bind-register", L"s13", L"0", L"33", L"0",
    L"-fvk-bind-register", L"s14", L"0", L"34", L"0",
    L"-fvk-bind-register", L"s15", L"0", L"35", L"0",
    L"-fvk-bind-register", L"u0", L"0", L"36", L"0",
    L"-fvk-bind-register", L"u1", L"0", L"37", L"0",
    L"-fvk-bind-register", L"u2", L"0", L"38", L"0",
    L"-fvk-bind-register", L"u3", L"0", L"39", L"0",
};
static constexpr uint32_t kCommonBindArgCount = uint32_t(sizeof(kCommonBindArgs) / sizeof(kCommonBindArgs[0]));

// Per-stage b1 binding (binding 1 for VS, 2 for PS, 3 for GS).
static const wchar_t* const kArgsVS[] = {
    L"-fvk-bind-register", L"b1", L"0", L"1", L"0",
};
static const wchar_t* const kArgsPS[] = {
    L"-fvk-bind-register", L"b1", L"0", L"2", L"0",
};
static const wchar_t* const kArgsGS[] = {
    L"-fvk-bind-register", L"b1", L"0", L"3", L"0",
};

static std::vector<const wchar_t*> StageArgs(int stage)
{
    std::vector<const wchar_t*> args;
    args.reserve(kCommonBindArgCount + 5);
    for (uint32_t i = 0; i < kCommonBindArgCount; ++i)
        args.push_back(kCommonBindArgs[i]);

    const wchar_t* const* extra = nullptr;
    uint32_t count = 0;
    switch (stage) {
        case Shader::kStageVS: extra = kArgsVS; count = uint32_t(sizeof(kArgsVS) / sizeof(kArgsVS[0])); break;
        case Shader::kStagePS: extra = kArgsPS; count = uint32_t(sizeof(kArgsPS) / sizeof(kArgsPS[0])); break;
        case Shader::kStageGS: extra = kArgsGS; count = uint32_t(sizeof(kArgsGS) / sizeof(kArgsGS[0])); break;
    }
    for (uint32_t i = 0; i < count; ++i)
        args.push_back(extra[i]);
    return args;
}

}  // namespace


bool Shader::Compile(const char* hlslCode, const char* entryPoint, const char* target,
                     std::vector<uint8_t>& spirvOut) noexcept
{
    if ((not hlslCode) or (not *hlslCode))
        return false;

    int stage = kStageVS;
    if (std::strncmp(target, "ps_", 3) == 0)
        stage = kStagePS;
    else if (std::strncmp(target, "gs_", 3) == 0)
        stage = kStageGS;

    auto args = StageArgs(stage);
    String error;
    if (not ShaderCompiler::CompileHlslToSpirv(hlslCode, entryPoint, target,
                                               args.data(), uint32_t(args.size()),
                                               spirvOut, error)) {
        fprintf(stderr, "Shader '%s': compile failed (entry=%s, target=%s):\n%s\n",
                (const char*)m_name, entryPoint, target, (const char*)error);
        return false;
    }
    return true;
}

// =================================================================================================
// CreatePipelineLayout — descriptor set layout (40 bindings: see shader.h table) + pipeline layout

bool Shader::CreatePipelineLayout(void) noexcept
{
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return false;

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(kBindingCount);

    auto addBinding = [&](uint32_t binding, VkDescriptorType type, uint32_t count, VkShaderStageFlags stages) {
        VkDescriptorSetLayoutBinding b { };
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = count;
        b.stageFlags = stages;
        b.pImmutableSamplers = nullptr;
        bindings.push_back(b);
    };

    addBinding(kBindingB0,   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_ALL_GRAPHICS);
    addBinding(kBindingB1VS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT);
    addBinding(kBindingB1PS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    addBinding(kBindingB1GS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_GEOMETRY_BIT);

    for (uint32_t i = 0; i < kSrvSlots; ++i)
        addBinding(kSrvBase + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

    for (uint32_t i = 0; i < kSamplerSlots; ++i)
        addBinding(kSamplerBase + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

    for (uint32_t i = 0; i < kUavSlots; ++i)
        addBinding(kUavBase + i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL_GRAPHICS);

    VkDescriptorSetLayoutCreateInfo setInfo { };
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = uint32_t(bindings.size());
    setInfo.pBindings = bindings.data();

    VkResult res = vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &m_setLayout);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Shader '%s': vkCreateDescriptorSetLayout failed (%d)\n", (const char*)m_name, (int)res);
        return false;
    }

    VkPipelineLayoutCreateInfo plInfo { };
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_setLayout;

    res = vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Shader '%s': vkCreatePipelineLayout failed (%d)\n", (const char*)m_name, (int)res);
        return false;
    }
    return true;
}


// Slot mapping matches GfxDataLayout::FixedSlotForBuffer exactly — must stay in sync.
//   slot 0: Vertex, slot 1-3: TexCoord/0-2, slot 4: Color,
//   slot 5: Normal, slot 6: Tangent, slot 7+: Offset/Float
static int SlotForAttr(const char* datatype, int id) noexcept
{
    if (strcmp(datatype, "Vertex") == 0)
        return 0;
    if (strcmp(datatype, "TexCoord") == 0) {
        if ((id >= 0) and (id <= 2))
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


static VkFormat VkFormatForAttr(ShaderDataAttributes::Format f) noexcept
{
    switch (f) {
        case ShaderDataAttributes::Float1: return VK_FORMAT_R32_SFLOAT;
        case ShaderDataAttributes::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case ShaderDataAttributes::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case ShaderDataAttributes::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}


static uint32_t StrideForFormat(ShaderDataAttributes::Format f) noexcept
{
    switch (f) {
        case ShaderDataAttributes::Float1: return 4;
        case ShaderDataAttributes::Float2: return 8;
        case ShaderDataAttributes::Float3: return 12;
        case ShaderDataAttributes::Float4: return 16;
    }
    return 0;
}


void Shader::BuildVertexInput(void) noexcept
{
    m_vsInputAttributes.clear();
    m_vsInputBindings.clear();

    if (m_dataLayout.m_count <= 0)
        return;

    // One VkVertexInputBindingDescription per slot, one VkVertexInputAttributeDescription
    // per declared attribute. Each slot reads one C++ buffer with offset 0 (the GfxDataBuffer
    // model — separate buffers, no interleaving). Stride is the per-vertex size of that slot's
    // format. SPIR-V locations follow the slot numbering (HLSL semantic order is preserved by
    // DXC in the order POSITION → TEXCOORD0..2 → COLOR → NORMAL → TANGENT → OFFSET0..3, which
    // matches SlotForAttr); explicit [[vk::location(N)]] would tighten this, but is not strictly
    // required as long as the HLSL declarations are kept in the same canonical order.
    m_vsInputAttributes.reserve(size_t(m_dataLayout.m_count));
    m_vsInputBindings.reserve(size_t(m_dataLayout.m_count));

    for (int i = 0; i < m_dataLayout.m_count; ++i) {
        const ShaderDataAttributes& attr = m_dataLayout.m_attrs[i];
        int slot = SlotForAttr(attr.datatype, attr.id);
        if (slot < 0)
            continue;

        VkVertexInputAttributeDescription a { };
        a.location = uint32_t(slot);
        a.binding = uint32_t(slot);
        a.format = VkFormatForAttr(attr.format);
        a.offset = 0;
        m_vsInputAttributes.push_back(a);

        VkVertexInputBindingDescription b { };
        b.binding = uint32_t(slot);
        b.stride = StrideForFormat(attr.format);
        b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        m_vsInputBindings.push_back(b);
    }
}


void Shader::UpdateStageFields(const std::vector<uint8_t>& spirv, int stage) noexcept
{
    // SPIR-V reflection of the per-stage ShaderConstants UBO (binding kBindingB1VS/PS/GS).
    // 1:1 functional equivalent of the DX12 ID3D12ShaderReflection path: enumerate descriptor
    // bindings, locate the dynamic UBO at the binding for this stage, walk its struct members
    // and append (name, {offset, size}) into m_stages[stage].fields.
    if (spirv.empty())
        return;

    SpvReflectShaderModule module{};
    if (spvReflectCreateShaderModule(spirv.size(), spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
        return;

    StageConstants& sc = m_stages[stage];
    const uint32_t targetBinding = (stage == kStageVS) ? kBindingB1VS
                                 : (stage == kStagePS) ? kBindingB1PS
                                                       : kBindingB1GS;

    uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding*> bindings(count);
    spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

    for (auto* b : bindings) {
        if (not b)
            continue;
        if (b->set != 0 or b->binding != targetBinding)
            continue;
        const SpvReflectDescriptorType t = b->descriptor_type;
        if (t != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER
            and t != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            continue;
        }
        if (b->block.size > sc.size)
            sc.size = b->block.size;
        for (uint32_t j = 0; j < b->block.member_count; ++j) {
            const SpvReflectBlockVariable& m = b->block.members[j];
            if (not m.name)
                continue;
            String name(m.name);
            bool found = false;
            for (auto& kv : sc.fields)
                if (kv.first == name) {
                    found = true;
                    break;
                }
            if (not found) {
                auto* entry = sc.fields.Append();
                if (entry)
                    *entry = { name, { m.offset, m.size } };
            }
        }
        break;
    }

    spvReflectDestroyShaderModule(&module);
}


bool Shader::Create(const String& vsCode, const String& fsCode, const String& gsCode)
{
    if (IsValid())
        return true;

    if (not Compile((const char*)vsCode, "VSMain", "vs_6_0", m_vsSpirv))
        return false;
    if (not Compile((const char*)fsCode, "PSMain", "ps_6_0", m_fsSpirv))
        return false;
    if (not gsCode.IsEmpty()) {
        if (not Compile((const char*)gsCode, "GSMain", "gs_6_0", m_gsSpirv))
            return false;
    }

    m_vsModule = ShaderCompiler::CreateShaderModule(m_vsSpirv);
    m_fsModule = ShaderCompiler::CreateShaderModule(m_fsSpirv);
    if ((m_vsModule == VK_NULL_HANDLE) or (m_fsModule == VK_NULL_HANDLE))
        return false;
    if (not m_gsSpirv.empty()) {
        m_gsModule = ShaderCompiler::CreateShaderModule(m_gsSpirv);
        if (m_gsModule == VK_NULL_HANDLE)
            return false;
    }

    if (not CreatePipelineLayout())
        return false;

    BuildVertexInput();
    UpdateStageFields(m_vsSpirv, kStageVS);
    UpdateStageFields(m_fsSpirv, kStagePS);
    if (not m_gsSpirv.empty())
        UpdateStageFields(m_gsSpirv, kStageGS);

    // Allocate per-stage staging buffers sized to the reflected b1 size.
    for (int s = 0; s < kStageCount; ++s) {
        if (m_stages[s].size > 0)
            m_stages[s].staging.assign(m_stages[s].size, 0);
        m_stages[s].dirty = true;
    }

    m_vs = vsCode;
    m_fs = fsCode;
    m_gs = gsCode;
    return true;
}


void Shader::Destroy(void) noexcept
{
    VkDevice device = vkContext.Device();

    pipelineCache.RemoveShader(this);

    for (int s = 0; s < kStageCount; ++s) {
        m_stages[s].fields.Clear();
        m_stages[s].staging.clear();
        m_stages[s].size = 0;
        m_stages[s].dirty = true;
    }
    m_vsInputAttributes.clear();
    m_vsInputBindings.clear();

    if (device != VK_NULL_HANDLE) {
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
        if (m_setLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
            m_setLayout = VK_NULL_HANDLE;
        }
        ShaderCompiler::DestroyShaderModule(m_vsModule);
        ShaderCompiler::DestroyShaderModule(m_fsModule);
        ShaderCompiler::DestroyShaderModule(m_gsModule);
    }
    m_vsModule = VK_NULL_HANDLE;
    m_fsModule = VK_NULL_HANDLE;
    m_gsModule = VK_NULL_HANDLE;
    m_vsSpirv.clear();
    m_fsSpirv.clear();
    m_gsSpirv.clear();
}


Shader& Shader::Copy(const Shader& other)
{
    // Shaders are not copyable (GPU resources), just copy metadata.
    m_name = other.m_name;
    m_vs = other.m_vs;
    m_fs = other.m_fs;
    m_gs = other.m_gs;
    return *this;
}


Shader& Shader::Move(Shader& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_name = std::move(other.m_name);
        m_vs = std::move(other.m_vs);
        m_fs = std::move(other.m_fs);
        m_gs = std::move(other.m_gs);
        m_vsSpirv = std::move(other.m_vsSpirv);
        m_fsSpirv = std::move(other.m_fsSpirv);
        m_gsSpirv = std::move(other.m_gsSpirv);
        m_vsModule = std::exchange(other.m_vsModule, VkShaderModule(VK_NULL_HANDLE));
        m_fsModule = std::exchange(other.m_fsModule, VkShaderModule(VK_NULL_HANDLE));
        m_gsModule = std::exchange(other.m_gsModule, VkShaderModule(VK_NULL_HANDLE));
        m_pipelineLayout = std::exchange(other.m_pipelineLayout, VkPipelineLayout(VK_NULL_HANDLE));
        m_setLayout = std::exchange(other.m_setLayout, VkDescriptorSetLayout(VK_NULL_HANDLE));
        m_b0Staging = other.m_b0Staging;
        for (int s = 0; s < kStageCount; ++s) {
            m_stages[s].size = other.m_stages[s].size;
            m_stages[s].staging = std::move(other.m_stages[s].staging);
            m_stages[s].dirty = other.m_stages[s].dirty;
            m_stages[s].fields = std::move(other.m_stages[s].fields);
        }
        m_vsInputAttributes = std::move(other.m_vsInputAttributes);
        m_vsInputBindings = std::move(other.m_vsInputBindings);
    }
    return *this;
}

// =================================================================================================
// Activate / Upload — Vulkan implementation.
//
// Activate                — vkCmdBindPipeline of the matching cached VkPipeline.
// UploadB0 / UploadB1     — sub-allocate from cbvAllocator and memcpy the staging buffers.
// UpdateVariables         — UploadB0 + UploadB1 + materialize the bind table into a
//                           VkDescriptorSet, then vkCmdBindDescriptorSets with dynamic offsets.

bool Shader::Activate(void)
{
    CommandList* cl = commandListHandler.CurrentCmdList();
    if (cl == nullptr)
        return false;
    VkPipeline pipeline = cl->GetPipeline(this);
    return pipeline != VK_NULL_HANDLE;
}


bool Shader::UploadB0(void) noexcept
{
    CbAlloc a = cbvAllocator.Allocate(uint32_t(sizeof(FrameConstants)));
    if (not a.IsValid())
        return false;
    std::memcpy(a.cpu, &m_b0Staging, sizeof(FrameConstants));
    m_dynamicOffsets[0] = a.offset;  // binding 0 (b0)
    return true;
}


bool Shader::UploadB1(void) noexcept
{
    // Per stage: sub-allocate sc.size bytes from cbvAllocator, memcpy sc.staging.data(),
    // store the dynamic offset at the matching m_dynamicOffsets slot. Stages with size == 0
    // are skipped (mirror DX12 behaviour); UpdateVariables writes a 1-byte placeholder range
    // for those bindings so the descriptor-set update stays valid.
    for (int s = 0; s < kStageCount; ++s) {
        StageConstants& sc = m_stages[s];
        if (sc.size == 0)
            continue;
        CbAlloc a = cbvAllocator.Allocate(sc.size);
        if (not a.IsValid())
            return false;
        std::memcpy(a.cpu, sc.staging.data(), sc.size);
        m_dynamicOffsets[1 + s] = a.offset;  // bindings 1 (VS), 2 (PS), 3 (GS)
        sc.dirty = false;
    }
    return true;
}

// =================================================================================================
// API-neutral helpers (1:1 from DX12)

bool Shader::UpdateMatrices(void)
{
    std::memcpy(m_b0Staging.mModelView, baseRenderer.ModelView().AsArray(), 16 * sizeof(float));
    std::memcpy(m_b0Staging.mProjection, baseRenderer.Projection().AsArray(), 16 * sizeof(float));
    std::memcpy(m_b0Staging.mViewport, baseRenderer.ViewportTransformation().AsArray(), 16 * sizeof(float));
    if (shadowMap.IsReady())
        std::memcpy(m_b0Staging.mLightTransform, shadowMap.GetTransformation().AsArray(), 16 * sizeof(float));
    return true;
}


// place all shader variables in CL; to be called right before the actual shader call.
// Vulkan: also materialize the staged bind table into a VkDescriptorSet and bind it on the
// active CB with the dynamic offsets just produced by UploadB0 / UploadB1.
bool Shader::UpdateVariables(void) noexcept {
    if (not UploadB0())
        return false;
    if (not UploadB1())
        return false;

    VkCommandBuffer cb = commandListHandler.CurrentGfxList();
    if (cb == VK_NULL_HANDLE or m_pipelineLayout == VK_NULL_HANDLE or m_setLayout == VK_NULL_HANDLE)
        return false;

    VkDescriptorSet set = descriptorPoolHandler.Allocate(m_setLayout);
    if (set == VK_NULL_HANDLE)
        return false;

    VkBuffer ubo = cbvAllocator.CurrentBuffer();
    if (ubo == VK_NULL_HANDLE)
        return false;

    // Worst-case write count: 4 dynamic UBOs + kSrvSlots images + kSamplerSlots samplers
    // + kUavSlots storage images.
    constexpr uint32_t kMaxWrites = 4 + 16 + 16 + 4;
    VkWriteDescriptorSet writes[kMaxWrites] { };
    VkDescriptorBufferInfo bufInfos[4]                              { };
    VkDescriptorImageInfo  imgInfos[CommandListHandler::kSrvSlots]  { };
    VkDescriptorImageInfo  smpInfos[CommandListHandler::kSamplerSlots] { };
    VkDescriptorImageInfo  stoInfos[CommandListHandler::kUavSlots]  { };
    uint32_t writeCount = 0;

    auto AddDynamicUbo = [&](uint32_t binding, uint32_t bytes, uint32_t bufSlot) {
        bufInfos[bufSlot].buffer = ubo;
        bufInfos[bufSlot].offset = 0;
        bufInfos[bufSlot].range  = (bytes > 0) ? bytes : 1;
        VkWriteDescriptorSet& w = writes[writeCount++];
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = binding;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        w.pBufferInfo     = &bufInfos[bufSlot];
    };

    AddDynamicUbo(kBindingB0,    uint32_t(sizeof(FrameConstants)),    0);
    AddDynamicUbo(kBindingB1VS,  m_stages[kStageVS].size,             1);
    AddDynamicUbo(kBindingB1PS,  m_stages[kStagePS].size,             2);
    AddDynamicUbo(kBindingB1GS,  m_stages[kStageGS].size,             3);

    // Sampled images (t-slots). Only slots with a non-null view are written; unbound slots
    // keep whatever the descriptor pool initialized (validation will warn if a shader actually
    // samples an unbound slot).
    for (uint32_t i = 0; i < CommandListHandler::kSrvSlots; ++i) {
        VkImageView v = commandListHandler.m_boundSrvViews[i];
        if (v == VK_NULL_HANDLE)
            continue;
        imgInfos[i].imageView   = v;
        imgInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfos[i].sampler     = VK_NULL_HANDLE;
        VkWriteDescriptorSet& w = writes[writeCount++];
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = kSrvBase + i;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        w.pImageInfo      = &imgInfos[i];
    }
    for (uint32_t i = 0; i < CommandListHandler::kSamplerSlots; ++i) {
        VkSampler s = commandListHandler.m_boundSamplers[i];
        if (s == VK_NULL_HANDLE)
            continue;
        smpInfos[i].sampler = s;
        VkWriteDescriptorSet& w = writes[writeCount++];
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = kSamplerBase + i;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
        w.pImageInfo      = &smpInfos[i];
    }
    for (uint32_t i = 0; i < CommandListHandler::kUavSlots; ++i) {
        VkImageView v = commandListHandler.m_boundStorageViews[i];
        if (v == VK_NULL_HANDLE)
            continue;
        stoInfos[i].imageView   = v;
        stoInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet& w = writes[writeCount++];
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = kUavBase + i;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.pImageInfo      = &stoInfos[i];
    }

    if (writeCount > 0)
        vkUpdateDescriptorSets(vkContext.Device(), writeCount, writes, 0, nullptr);

    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &set,
                            kDynamicOffsetCount, m_dynamicOffsets);
    return true;
}

// =================================================================================================
// Uniform setters (1:1 from DX12 — operate purely on the CPU staging buffers)

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
    if (TrySetB0Field(name, data))
        return 0;
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
