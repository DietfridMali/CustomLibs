#define NOMINMAX

#include <utility>
#include <cstring>
#include <limits>
#include <vector>

#include "vkframework.h"
#include "compute_shader.h"
#include "shader_compiler.h"
#include "vkcontext.h"
#include "cbv_allocator.h"
#include <spirv_reflect.h>

// =================================================================================================
// Vulkan ComputeShader implementation (2026-05-18)
//
// HLSL compute source compiled via DXC to SPIR-V (target cs_6_0). Bindings are described by
// the caller via an AutoArray<ComputeBindingDesc> — flexible vs. the graphics Shader's fixed
// 40-binding layout. Pipeline-Layout + DescriptorSetLayout are built per-instance from those.
//
// Binding-register mapping (DXC -fvk-bind-register) matches the graphics shader on the b/t/s/u
// registers so that shared HLSL snippets (cbuffer ShaderConstants, Texture3D shapeNoiseTex at
// t2, ...) read at the same SPIR-V binding numbers and can be reused across graphics and
// compute. Only the b1 register binding differs (compute uses binding 1; graphics splits b1
// into 1=VS, 2=PS, 3=GS).
//
// The Activate/Dispatch/Bind methods are minimal — the actual descriptor-set acquisition,
// resource binding, and vkCmdDispatch issuing happens in the caller (CloudRenderer's TSP
// pipeline). This keeps the compute-shader class small and lets the cloud-side decide pool
// strategy without hauling pool state through this class.
// =================================================================================================

namespace {

static VkDescriptorType ToVkDescriptorType(ComputeBindingDesc::Kind kind) {
    switch (kind) {
        case ComputeBindingDesc::Kind::UniformBuffer:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case ComputeBindingDesc::Kind::SampledImage:         return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case ComputeBindingDesc::Kind::StorageImage:         return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case ComputeBindingDesc::Kind::StorageBuffer:        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case ComputeBindingDesc::Kind::Sampler:              return VK_DESCRIPTOR_TYPE_SAMPLER;
        case ComputeBindingDesc::Kind::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

// Compile-time binding-register args for HLSL → SPIR-V. Must match graphics shader's layout
// so shared snippets (cbuffer ShaderConstants, Texture3D shapeNoiseTex t2, ...) bind to the
// same SPIR-V binding numbers. Only b1 differs (compute: 1; graphics: 1/2/3 per stage).
static const wchar_t* const kComputeBindArgs[] = {
    L"-fvk-bind-register", L"b0", L"0", L"0", L"0",
    L"-fvk-bind-register", L"b1", L"0", L"1", L"0",
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
static constexpr uint32_t kComputeBindArgCount = uint32_t(sizeof(kComputeBindArgs) / sizeof(kComputeBindArgs[0]));

}  // namespace


bool ComputeShader::Compile(const char* hlslCode, const char* entryPoint, std::vector<uint8_t>& spirvOut) noexcept
{
    if ((not hlslCode) or (not *hlslCode))
        return false;

    String error;
    if (not ShaderCompiler::CompileHlslToSpirv(hlslCode, entryPoint, "cs_6_0",
                                               kComputeBindArgs, kComputeBindArgCount,
                                               spirvOut, error)) {
        fprintf(stderr, "ComputeShader '%s': compile failed (entry=%s):\n%s\n",
                (const char*)m_name, entryPoint, (const char*)error);
        return false;
    }
    return true;
}


bool ComputeShader::CreatePipelineLayout(const AutoArray<ComputeBindingDesc>& bindings) noexcept
{
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return false;

    std::vector<VkDescriptorSetLayoutBinding> dsBindings;
    dsBindings.reserve(size_t(bindings.Length()));

    for (int i = 0; i < bindings.Length(); ++i) {
        const ComputeBindingDesc& b = bindings[i];
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = b.binding;
        lb.descriptorType = ToVkDescriptorType(b.kind);
        lb.descriptorCount = b.count;
        lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        lb.pImmutableSamplers = nullptr;
        dsBindings.push_back(lb);
    }

    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = uint32_t(dsBindings.size());
    setInfo.pBindings = dsBindings.data();

    VkResult res = vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &m_setLayout);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "ComputeShader '%s': vkCreateDescriptorSetLayout failed (%d)\n", (const char*)m_name, (int)res);
        return false;
    }

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_setLayout;

    res = vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "ComputeShader '%s': vkCreatePipelineLayout failed (%d)\n", (const char*)m_name, (int)res);
        return false;
    }
    return true;
}


bool ComputeShader::CreatePipeline(void) noexcept
{
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE or m_csModule == VK_NULL_HANDLE or m_pipelineLayout == VK_NULL_HANDLE)
        return false;

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = m_csModule;
    stage.pName = "CSMain";

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage = stage;
    info.layout = m_pipelineLayout;

    VkResult res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "ComputeShader '%s': vkCreateComputePipelines failed (%d)\n", (const char*)m_name, (int)res);
        return false;
    }
    return true;
}


bool ComputeShader::Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings)
{
    if (IsValid())
        return true;

    if (not Compile((const char*)csCode, "CSMain", m_csSpirv))
        return false;

    m_csModule = ShaderCompiler::CreateShaderModule(m_csSpirv);
    if (m_csModule == VK_NULL_HANDLE)
        return false;

    m_bindings = bindings;

    if (not CreatePipelineLayout(bindings))
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
    // SPIR-V reflection of the b1 cbuffer (set 0, binding 1 per kComputeBindArgs).
    if (m_csSpirv.empty())
        return;

    SpvReflectShaderModule mod{};
    if (spvReflectCreateShaderModule(m_csSpirv.size(), m_csSpirv.data(), &mod) != SPV_REFLECT_RESULT_SUCCESS)
        return;

    uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(&mod, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding*> bs(count);
    spvReflectEnumerateDescriptorBindings(&mod, &count, bs.data());

    for (auto* b : bs) {
        if (not b)
            continue;
        if (b->set != 0 or b->binding != 1u)
            continue;
        const SpvReflectDescriptorType t = b->descriptor_type;
        if (t != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER
            and t != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
            continue;
        if (b->block.size > m_b1Size)
            m_b1Size = b->block.size;
        for (uint32_t j = 0; j < b->block.member_count; ++j) {
            const SpvReflectBlockVariable& m = b->block.members[j];
            if (not m.name)
                continue;
            String name(m.name);
            bool found = false;
            for (auto& kv : m_b1Fields)
                if (kv.first == name) {
                    found = true;
                    break;
                }
            if (not found) {
                auto* entry = m_b1Fields.Append();
                if (entry)
                    *entry = { name, { m.offset, m.size } };
            }
        }
        break;
    }
    spvReflectDestroyShaderModule(&mod);
}


void ComputeShader::Destroy(void) noexcept
{
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return;
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
    ShaderCompiler::DestroyShaderModule(m_csModule);
    m_csModule = VK_NULL_HANDLE;
    m_csSpirv.clear();
    m_b1Staging.clear();
    m_b1Fields.Reset();
    m_b1Size = 0;
    m_b1Dirty = true;
}


// -------------------------------------------------------------------------------------------------
// Runtime (Activate / Dispatch / Bind)
//
// These methods are intentionally lightweight. The caller (CloudRenderer for the TSP pass)
// drives descriptor-set acquisition from its own pool, fills writes for the resources it has,
// and issues vkCmdDispatch directly with the right command buffer. See TSP integration in
// cloudrenderer.cpp for the actual wiring.
// -------------------------------------------------------------------------------------------------

bool ComputeShader::Activate(void)
{
    if (not IsValid())
        return false;
    // Caller binds the pipeline via vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline).
    // The wrapper used to be richer in the graphics path because PSOs are state-set-dependent;
    // for compute the bind is trivial so we leave that single call to the caller alongside the
    // descriptor-set bind it must do anyway.
    return true;
}


bool ComputeShader::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    if (not IsValid())
        return false;
    // Caller issues vkCmdDispatch(cb, groupCountX, groupCountY, groupCountZ) after Activate +
    // descriptor-set bind. See cloudrenderer.cpp TSP path for usage.
    (void)groupCountX; (void)groupCountY; (void)groupCountZ;
    return true;
}


bool ComputeShader::Dispatch2D(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY)
{
    if (tileX == 0 or tileY == 0)
        return false;
    uint32_t gx = (width  + tileX - 1) / tileX;
    uint32_t gy = (height + tileY - 1) / tileY;
    return Dispatch(gx, gy, 1);
}


bool ComputeShader::BindSampledImage(uint32_t binding, Texture* texture, uint32_t arrayIndex)
{
    (void)binding; (void)texture; (void)arrayIndex;
    // Caller writes a VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE write into its per-pass descriptor set.
    return true;
}


bool ComputeShader::BindStorageImage(uint32_t binding, RenderTarget* target, int bufferIndex, uint32_t arrayIndex)
{
    (void)binding; (void)target; (void)bufferIndex; (void)arrayIndex;
    // Caller writes a VK_DESCRIPTOR_TYPE_STORAGE_IMAGE write into its per-pass descriptor set.
    // The target's color buffer must be transitioned to VK_IMAGE_LAYOUT_GENERAL before dispatch.
    return true;
}


bool ComputeShader::BindSampler(uint32_t binding, VkSampler sampler)
{
    (void)binding; (void)sampler;
    return true;
}


int ComputeShader::SetB1(uint32_t offset, const void* data, size_t size) noexcept
{
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
    CbAlloc a = cbvAllocator.Allocate(m_b1Size);
    if (not a.IsValid())
        return false;
    std::memcpy(a.cpu, m_b1Staging.data(), m_b1Size);
    m_b1DynamicOffset = a.offset;
    m_b1Dirty = false;
    return true;
}

// =================================================================================================
