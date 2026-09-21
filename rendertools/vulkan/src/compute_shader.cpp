#define NOMINMAX

#include <utility>
#include <cstring>
#include <limits>
#include <vector>

#include "vkframework.h"
#include "compute_shader.h"
#include "shader.h"
#include "shader_compiler.h"
#include "pipeline_cache.h"
#include "vkcontext.h"
#include "cbv_allocator.h"
#include "commandlist.h"
#include "descriptor_pool_handler.h"
#include "vkupload.h"
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

static const wchar_t* const kComputeArgsAccel[] = {
    L"-fvk-bind-register", L"t0", L"2", L"62", L"0",
};
static_assert(ComputeShader::kBindingAccel == 62, "kComputeArgsAccel names the acceleration structure binding by number");
static_assert(ComputeShader::kAccelSpace == 2, "kComputeArgsAccel names the acceleration structure register space by number");
static_assert(ComputeShader::kBindingAccel == Shader::kBindingAccel, "compute and graphics shaders share the HLSL declaration of the acceleration structure");
static_assert(ComputeShader::kAccelSpace == Shader::kAccelSpace, "compute and graphics shaders share the HLSL declaration of the acceleration structure");

static constexpr const char* kAccelTypeName = "RaytracingAccelerationStructure";

}  // namespace


bool ComputeShader::Compile(const char* hlslCode, const char* entryPoint, std::vector<uint8_t>& spirvOut, const String& shaderFolder)
{
    if ((not hlslCode) or (not *hlslCode))
        return false;

    std::vector<const wchar_t*> args;
    args.reserve(kComputeBindArgCount + 5);
    for (uint32_t i = 0; i < kComputeBindArgCount; ++i)
        args.push_back(kComputeBindArgs[i]);

    const char* target = "cs_6_0";
    if (std::strstr(hlslCode, kAccelTypeName) != nullptr) {
        for (const wchar_t* arg : kComputeArgsAccel)
            args.push_back(arg);
        target = "cs_6_5";
    }

    String error;
    if (not ShaderCompiler::CompileHlslToSpirv(hlslCode, entryPoint, target,
                                               args.data(), uint32_t(args.size()),
                                               spirvOut, error,
                                               shaderFolder, m_name + String(".") + String(target) + String(ShaderCompiler::kOptimizationLevel) + String(".spv"))) {
        fprintf(stderr, "ComputeShader '%s': compile failed (entry=%s, target=%s):\n%s\n",
                (const char*)m_name, entryPoint, target, (const char*)error);
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

    if (m_usesAccelStructure) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = kBindingAccel;
        lb.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        lb.descriptorCount = 1;
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

    VkResult res = vkCreateComputePipelines(device, pipelineCache.m_pipelineCache, 1, &info, nullptr, &m_pipeline);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "ComputeShader '%s': vkCreateComputePipelines failed (%d)\n", (const char*)m_name, (int)res);
        return false;
    }
    return true;
}


bool ComputeShader::Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings, const String& shaderFolder)
{
    if (IsValid())
        return true;

    m_usesAccelStructure = std::strstr(static_cast<const char*>(csCode), kAccelTypeName) != nullptr;
    if (m_usesAccelStructure and not vkContext.HasRayTracing()) {
        fprintf(stderr, "ComputeShader '%s': needs ray tracing, which this device does not have - not created\n", (const char*)m_name);
        m_usesAccelStructure = false;
        return false;
    }

    if (not Compile((const char*)csCode, "CSMain", m_csSpirv, shaderFolder))
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
    m_usesAccelStructure = false;
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


// A whole compute pass of its own, outside the frame: the per-frame path leaves descriptor set and
// vkCmdDispatch to the caller because it has the frame's command buffer; here there is none, so this
// takes the same route GfxArray::Download () does - a transient command buffer that is submitted and
// waited for. The storage buffers come from what the caller bound through GfxArray::Bind (), the same
// state the graphics path materializes from.

bool ComputeShader::DispatchOnce(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    if (not IsValid())
        return false;
    if ((groupCountX == 0) or (groupCountY == 0) or (groupCountZ == 0))
        return false;

    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return false;

    VkAccelerationStructureKHR accelStructure = commandListHandler.m_boundAccelStructure;
    if (m_usesAccelStructure and (accelStructure == VK_NULL_HANDLE)) {
        fprintf(stderr, "ComputeShader '%s': declares an acceleration structure, but none is bound\n", (const char*)m_name);
        return false;
    }

    VkDescriptorSet set = descriptorPoolHandler.Allocate(m_setLayout);
    if (set == VK_NULL_HANDLE)
        return false;

    if ((m_b1Size > 0) and not UploadB1())
        return false;

    VkWriteDescriptorSet    writes[CommandListHandler::kUavSlots + 2]{};
    VkDescriptorBufferInfo  bufferInfos[CommandListHandler::kUavSlots + 1]{};
    uint32_t                writeCount = 0;
    uint32_t                dynamicOffset = 0;
    uint32_t                dynamicOffsetCount = 0;

    // b1 - the parameters the caller set through SetFloat / SetInt before the call
    if (m_b1Size > 0) {
        bufferInfos[writeCount].buffer = m_b1Buffer;
        bufferInfos[writeCount].offset = 0;
        bufferInfos[writeCount].range = m_b1Size;
        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].dstSet = set;
        writes[writeCount].dstBinding = 1;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[writeCount].pBufferInfo = &bufferInfos[writeCount];
        dynamicOffset = m_b1DynamicOffset;
        dynamicOffsetCount = 1;
        writeCount++;
    }

    // u0..u3 - HLSL register uN maps to descriptor binding 36 + N (see kComputeBindArgs)
    for (uint32_t slot = 0; slot < CommandListHandler::kUavSlots; slot++) {
        VkBuffer buffer = commandListHandler.m_boundStorageBuffers[slot];
        if (buffer == VK_NULL_HANDLE)
            continue;
        bufferInfos[writeCount].buffer = buffer;
        bufferInfos[writeCount].offset = 0;
        bufferInfos[writeCount].range = commandListHandler.m_boundStorageBufferSize[slot];
        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].dstSet = set;
        writes[writeCount].dstBinding = 36 + slot;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[writeCount].pBufferInfo = &bufferInfos[writeCount];
        writeCount++;
    }

    VkWriteDescriptorSetAccelerationStructureKHR accelInfo{};
    if (m_usesAccelStructure) {
        accelInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        accelInfo.accelerationStructureCount = 1;
        accelInfo.pAccelerationStructures = &accelStructure;
        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].pNext = &accelInfo;
        writes[writeCount].dstSet = set;
        writes[writeCount].dstBinding = kBindingAccel;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        writeCount++;
    }

    if (writeCount > 0)
        vkUpdateDescriptorSets(device, writeCount, writes, 0, nullptr);

    OneShotCommandBuffer once;
    if (not BeginSingleTimeCommands(once))
        return false;

    vkCmdBindPipeline(once.cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(once.cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &set,
                            dynamicOffsetCount, dynamicOffsetCount ? &dynamicOffset : nullptr);
    vkCmdDispatch(once.cb, groupCountX, groupCountY, groupCountZ);

    // the results are read back with a one-shot copy of its own, so the shader writes have to be
    // visible to transfer reads by then
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(once.cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    return EndSingleTimeCommands(once);
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
    for (auto& kv : m_b1Fields) {
        if (kv.first == name) {
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
    m_b1Buffer = a.buffer;
    m_b1Dirty = false;
    return true;
}

// =================================================================================================
