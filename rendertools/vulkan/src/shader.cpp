#define NOMINMAX

#include <utility>
#include <cstring>
#include <limits>
#include <vector>

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
#include "base_displayhandler.h"
#include "image_layout_tracker.h"
#include "sampler_cache.h"
#include "texturesampling.h"
#include "vkupload.h"
#include <spirv_reflect.h>

extern double VkStallClock(void) noexcept;
extern void VkStallEvent(const char* what, double startMs, const char* detail) noexcept;

// =================================================================================================
// Vulkan Shader implementation
//
// Descriptor-set layout (single set, all bindings — see shader.h for the full table).
// DXC compile uses -fvk-bind-register for every resource class, because b1 needs a
// stage-specific Vulkan binding (1/2/3 for VS/PS/GS) and DXC rejects mixing
// -fvk-bind-register with the -fvk-{b,t,s,u}-shift options. Bindings match the layout
// in CreatePipelineLayout: b0=0, b1=1/2/3/40/41, t0..t15=4..19, s0..s15=20..35, u0..u3=36..39,
// t0..t16 space1=42..58.

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
    L"-fvk-bind-register", L"t0", L"1", L"42", L"0",
    L"-fvk-bind-register", L"t1", L"1", L"43", L"0",
    L"-fvk-bind-register", L"t2", L"1", L"44", L"0",
    L"-fvk-bind-register", L"t3", L"1", L"45", L"0",
    L"-fvk-bind-register", L"t4", L"1", L"46", L"0",
    L"-fvk-bind-register", L"t5", L"1", L"47", L"0",
    L"-fvk-bind-register", L"t6", L"1", L"48", L"0",
    L"-fvk-bind-register", L"t7", L"1", L"49", L"0",
    L"-fvk-bind-register", L"t8", L"1", L"50", L"0",
    L"-fvk-bind-register", L"t9", L"1", L"51", L"0",
    L"-fvk-bind-register", L"t10", L"1", L"52", L"0",
    L"-fvk-bind-register", L"t11", L"1", L"53", L"0",
    L"-fvk-bind-register", L"t12", L"1", L"54", L"0",
    L"-fvk-bind-register", L"t13", L"1", L"55", L"0",
    L"-fvk-bind-register", L"t14", L"1", L"56", L"0",
    L"-fvk-bind-register", L"t15", L"1", L"57", L"0",
    L"-fvk-bind-register", L"t16", L"1", L"58", L"0",
    L"-fvk-bind-register", L"t17", L"1", L"59", L"0",
    L"-fvk-bind-register", L"t18", L"1", L"60", L"0",
    L"-fvk-bind-register", L"t19", L"1", L"61", L"0",
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
static const wchar_t* const kArgsHS[] = {
    L"-fvk-bind-register", L"b1", L"0", L"40", L"0",
};
static const wchar_t* const kArgsDS[] = {
    L"-fvk-bind-register", L"b1", L"0", L"41", L"0",
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
        case Shader::kStageHS: extra = kArgsHS; count = uint32_t(sizeof(kArgsHS) / sizeof(kArgsHS[0])); break;
        case Shader::kStageDS: extra = kArgsDS; count = uint32_t(sizeof(kArgsDS) / sizeof(kArgsDS[0])); break;
    }
    for (uint32_t i = 0; i < count; ++i)
        args.push_back(extra[i]);
    return args;
}


struct DefaultImage {
    VkImage             image { VK_NULL_HANDLE };
    VmaAllocation       allocation { VK_NULL_HANDLE };
    ImageLayoutTracker  tracker;
};


struct DefaultResources {
    DefaultImage    flat;
    DefaultImage    cube;
    DefaultImage    volume;
    VkImageView     views[5] { };
    bool            attempted { false };
};

static DefaultResources defaultResources;

static const uint8_t defaultPixel[4] = { 255, 255, 255, 255 };


static bool CreateDefaultImage(DefaultImage& target, uint32_t layers, VkImageCreateFlags flags) noexcept
{
    VmaAllocator allocator = vkContext.Allocator();
    if (allocator == VK_NULL_HANDLE)
        return false;

    VkImageCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.flags = flags;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent.width = 1;
    info.extent.height = 1;
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult res = vmaCreateImage(allocator, &info, &allocInfo, &target.image, &target.allocation, nullptr);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Shader: default image creation failed (%d)\n", int(res));
        return false;
    }
    target.tracker.Init(target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

    const uint8_t* faces[6] = { defaultPixel, defaultPixel, defaultPixel, defaultPixel, defaultPixel, defaultPixel };
    return UploadTextureData(target.image, target.tracker, faces, int(layers), 1, 1, 4);
}


static VkImageView CreateDefaultView(VkImage image, VkImageViewType viewType) noexcept
{
    VkDevice device = vkContext.Device();
    if ((device == VK_NULL_HANDLE) or (image == VK_NULL_HANDLE))
        return VK_NULL_HANDLE;

    VkImageViewCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.viewType = viewType;
    info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkImageView view = VK_NULL_HANDLE;
    VkResult res = vkCreateImageView(device, &info, nullptr, &view);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Shader: default image view creation failed (%d)\n", int(res));
        return VK_NULL_HANDLE;
    }
    return view;
}


static void CreateDefaultResources(void) noexcept
{
    DefaultResources& r = defaultResources;
    r.attempted = true;
    if (CreateDefaultImage(r.flat, 1, 0)) {
        r.views[Shader::dv2D] = CreateDefaultView(r.flat.image, VK_IMAGE_VIEW_TYPE_2D);
        r.views[Shader::dv2DArray] = CreateDefaultView(r.flat.image, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    }
    if (CreateDefaultImage(r.cube, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT))
        r.views[Shader::dvCube] = CreateDefaultView(r.cube.image, VK_IMAGE_VIEW_TYPE_CUBE);
    if (Upload3DTextureData(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, 4, defaultPixel, r.volume.image, r.volume.allocation, r.volume.tracker))
        r.views[Shader::dv3D] = CreateDefaultView(r.volume.image, VK_IMAGE_VIEW_TYPE_3D);
}


static VkImageView DefaultView(uint8_t viewType) noexcept
{
    if (viewType == Shader::dvNone)
        return VK_NULL_HANDLE;
    if (not defaultResources.attempted)
        CreateDefaultResources();
    return defaultResources.views[viewType];
}


static void DestroyDefaultImage(DefaultImage& target) noexcept
{
    VmaAllocator allocator = vkContext.Allocator();
    if ((allocator != VK_NULL_HANDLE) and (target.image != VK_NULL_HANDLE))
        vmaDestroyImage(allocator, target.image, target.allocation);
    target = DefaultImage { };
}

}  // namespace


void Shader::DestroyDefaultResources(void) noexcept
{
    DefaultResources& r = defaultResources;
    VkDevice device = vkContext.Device();
    for (VkImageView& view : r.views) {
        if ((device != VK_NULL_HANDLE) and (view != VK_NULL_HANDLE))
            vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    DestroyDefaultImage(r.flat);
    DestroyDefaultImage(r.cube);
    DestroyDefaultImage(r.volume);
    r.attempted = false;
}


bool Shader::Compile(const char* hlslCode, const char* entryPoint, const char* target,
                     std::vector<uint8_t>& spirvOut, const String& shaderFolder)
{
    if ((not hlslCode) or (not *hlslCode))
        return false;

    int stage = kStageVS;
    if (std::strncmp(target, "ps_", 3) == 0)
        stage = kStagePS;
    else if (std::strncmp(target, "gs_", 3) == 0)
        stage = kStageGS;
    else if (std::strncmp(target, "hs_", 3) == 0)
        stage = kStageHS;
    else if (std::strncmp(target, "ds_", 3) == 0)
        stage = kStageDS;

    auto args = StageArgs(stage);
    String error;
    if (not ShaderCompiler::CompileHlslToSpirv(hlslCode, entryPoint, target,
                                               args.data(), uint32_t(args.size()),
                                               spirvOut, error,
                                               shaderFolder, m_name + String(".") + String(target) + String(ShaderCompiler::kOptimizationLevel) + String(".spv"))) {
        fprintf(stderr, "Shader '%s': compile failed (entry=%s, target=%s):\n%s\n",
                (const char*)m_name, entryPoint, target, (const char*)error);
        return false;
    }
    return true;
}

// =================================================================================================
// CreatePipelineLayout — descriptor set layout (59 bindings: see shader.h table) + pipeline layout

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
        addBinding(kSrvBase + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_ALL_GRAPHICS);

    for (uint32_t i = 0; i < kSamplerSlots; ++i)
        addBinding(kSamplerBase + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_ALL_GRAPHICS);

    for (uint32_t i = 0; i < kUavSlots; ++i)
        addBinding(kUavBase + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS);

    addBinding(kBindingB1HS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    addBinding(kBindingB1DS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

    for (uint32_t i = 0; i < kSsboSlots; ++i)
        addBinding(kSsboBase + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS);

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


// Input slots come from the central vertex attribute registry (GfxAttributeSlot in
// shaderdatalayout.h), shared with GfxDataLayout and the other backends.


static VkFormat VkFormatForAttr(ShaderDataAttributes::Format f) noexcept
{
    switch (f) {
        case ShaderDataAttributes::Float1: return VK_FORMAT_R32_SFLOAT;
        case ShaderDataAttributes::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case ShaderDataAttributes::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case ShaderDataAttributes::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case ShaderDataAttributes::Uint1:  return VK_FORMAT_R32_UINT;
        case ShaderDataAttributes::Uint2:  return VK_FORMAT_R32G32_UINT;
        case ShaderDataAttributes::Uint3:  return VK_FORMAT_R32G32B32_UINT;
        case ShaderDataAttributes::Uint4:  return VK_FORMAT_R32G32B32A32_UINT;
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
        case ShaderDataAttributes::Uint1:  return 4;
        case ShaderDataAttributes::Uint2:  return 8;
        case ShaderDataAttributes::Uint3:  return 12;
        case ShaderDataAttributes::Uint4:  return 16;
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
    // format. location = slot requires every HLSL vertex input to carry an explicit
    // [[vk::location(N)]] with its registry slot — without it DXC numbers SPIR-V locations
    // densely in declaration order, which diverges as soon as the used slots have gaps.
    m_vsInputAttributes.reserve(size_t(m_dataLayout.m_count));
    m_vsInputBindings.reserve(size_t(m_dataLayout.m_count));

    for (int i = 0; i < m_dataLayout.m_count; ++i) {
        const ShaderDataAttributes& attr = m_dataLayout.m_attrs[i];
        int slot = GfxAttributeSlot(attr.datatype, attr.id);
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
                                 : (stage == kStageGS) ? kBindingB1GS
                                 : (stage == kStageHS) ? kBindingB1HS
                                                       : kBindingB1DS;

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


void Shader::UpdateStageResources(const std::vector<uint8_t>& spirv) noexcept
{
    if (spirv.empty())
        return;

    SpvReflectShaderModule module{};
    if (spvReflectCreateShaderModule(spirv.size(), spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
        return;

    uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding*> bindings(count);
    spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

    for (auto* b : bindings) {
        if (not b or (b->set != 0))
            continue;
        if ((b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER) and (b->binding >= kSamplerBase) and (b->binding < kSamplerBase + kSamplerSlots)) {
            m_samplerDeclared[b->binding - kSamplerBase] = true;
            continue;
        }
        if ((b->descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE) or (b->binding < kSrvBase) or (b->binding >= kSrvBase + kSrvSlots))
            continue;
        if (not b->type_description or not (b->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT))
            continue;
        if (b->image.ms != 0)
            continue;

        uint8_t viewType = dvNone;
        if (b->image.dim == SpvDim2D)
            viewType = b->image.arrayed ? dv2DArray : dv2D;
        else if ((b->image.dim == SpvDimCube) and not b->image.arrayed)
            viewType = dvCube;
        else if (b->image.dim == SpvDim3D)
            viewType = dv3D;
        m_srvDefaults[b->binding - kSrvBase] = viewType;
    }

    spvReflectDestroyShaderModule(&module);
}


uint32_t Shader::ReflectPatchControlPoints(const std::vector<uint8_t>& spirv) noexcept
{
    SpvReflectShaderModule module{};
    if (spvReflectCreateShaderModule(spirv.size(), spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
        return 0;
    uint32_t count = 0;
    spvReflectEnumerateInputVariables(&module, &count, nullptr);
    std::vector<SpvReflectInterfaceVariable*> inputs(count);
    spvReflectEnumerateInputVariables(&module, &count, inputs.data());
    uint32_t controlPoints = 0;
    for (SpvReflectInterfaceVariable* v : inputs) {
        if (v and (v->array.dims_count > 0) and (v->array.dims[0] > controlPoints))
            controlPoints = v->array.dims[0];
    }
    spvReflectDestroyShaderModule(&module);
    return controlPoints;
}


bool Shader::Create(const String& vsCode, const String& fsCode, const String& gsCode, const String& tcsCode, const String& tesCode, const String& shaderFolder)
{
    if (IsValid())
        return true;

    double stallStart = VkStallClock();
    if (tcsCode.IsEmpty() != tesCode.IsEmpty()) {
        fprintf(stderr, "Shader '%s': hull and domain shader must both be present\n", (const char*)m_name);
        return false;
    }
    if (not Compile((const char*)vsCode, "VSMain", "vs_6_0", m_vsSpirv, shaderFolder))
        return false;
    if (not Compile((const char*)fsCode, "PSMain", "ps_6_0", m_fsSpirv, shaderFolder))
        return false;
    if (not gsCode.IsEmpty()) {
        if (not Compile((const char*)gsCode, "GSMain", "gs_6_0", m_gsSpirv, shaderFolder))
            return false;
    }
    if (not tcsCode.IsEmpty()) {
        if (not Compile(static_cast<const char*>(tcsCode), "HSMain", "hs_6_0", m_hsSpirv, shaderFolder))
            return false;
        if (not Compile(static_cast<const char*>(tesCode), "DSMain", "ds_6_0", m_dsSpirv, shaderFolder))
            return false;
        m_patchControlPoints = ReflectPatchControlPoints(m_hsSpirv);
        if (m_patchControlPoints == 0) {
            fprintf(stderr, "Shader '%s': hull shader input patch size not found\n", (const char*)m_name);
            return false;
        }
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
    if (not m_hsSpirv.empty()) {
        m_hsModule = ShaderCompiler::CreateShaderModule(m_hsSpirv);
        m_dsModule = ShaderCompiler::CreateShaderModule(m_dsSpirv);
        if ((m_hsModule == VK_NULL_HANDLE) or (m_dsModule == VK_NULL_HANDLE))
            return false;
    }

    if (not CreatePipelineLayout())
        return false;

    BuildVertexInput();
    UpdateStageFields(m_vsSpirv, kStageVS);
    UpdateStageFields(m_fsSpirv, kStagePS);
    if (not m_gsSpirv.empty())
        UpdateStageFields(m_gsSpirv, kStageGS);
    if (not m_hsSpirv.empty())
        UpdateStageFields(m_hsSpirv, kStageHS);
    if (not m_dsSpirv.empty())
        UpdateStageFields(m_dsSpirv, kStageDS);

    UpdateStageResources(m_vsSpirv);
    UpdateStageResources(m_fsSpirv);
    UpdateStageResources(m_gsSpirv);
    UpdateStageResources(m_hsSpirv);
    UpdateStageResources(m_dsSpirv);

    // Allocate per-stage staging buffers sized to the reflected b1 size.
    for (int s = 0; s < kStageCount; ++s) {
        if (m_stages[s].size > 0)
            m_stages[s].staging.assign(m_stages[s].size, 0);
        m_stages[s].dirty = true;
    }

    m_vs = vsCode;
    m_fs = fsCode;
    m_gs = gsCode;
    VkStallEvent("shader create", stallStart, static_cast<const char*>(m_name));
    pipelineCache.CreateShaderLibraries(this);
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
    m_locations.Clear();
    m_vsInputAttributes.clear();
    m_vsInputBindings.clear();
    std::memset(m_srvDefaults, 0, sizeof(m_srvDefaults));
    std::memset(m_samplerDeclared, 0, sizeof(m_samplerDeclared));

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
        ShaderCompiler::DestroyShaderModule(m_hsModule);
        ShaderCompiler::DestroyShaderModule(m_dsModule);
    }
    m_vsModule = VK_NULL_HANDLE;
    m_fsModule = VK_NULL_HANDLE;
    m_gsModule = VK_NULL_HANDLE;
    m_hsModule = VK_NULL_HANDLE;
    m_dsModule = VK_NULL_HANDLE;
    m_patchControlPoints = 0;
    m_vsSpirv.clear();
    m_fsSpirv.clear();
    m_gsSpirv.clear();
    m_hsSpirv.clear();
    m_dsSpirv.clear();
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
        m_hsSpirv = std::move(other.m_hsSpirv);
        m_dsSpirv = std::move(other.m_dsSpirv);
        m_vsModule = std::exchange(other.m_vsModule, VkShaderModule(VK_NULL_HANDLE));
        m_fsModule = std::exchange(other.m_fsModule, VkShaderModule(VK_NULL_HANDLE));
        m_gsModule = std::exchange(other.m_gsModule, VkShaderModule(VK_NULL_HANDLE));
        m_hsModule = std::exchange(other.m_hsModule, VkShaderModule(VK_NULL_HANDLE));
        m_dsModule = std::exchange(other.m_dsModule, VkShaderModule(VK_NULL_HANDLE));
        m_patchControlPoints = other.m_patchControlPoints;
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
        std::memcpy(m_srvDefaults, other.m_srvDefaults, sizeof(m_srvDefaults));
        std::memcpy(m_samplerDeclared, other.m_samplerDeclared, sizeof(m_samplerDeclared));
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
    // Nothing on the draw buffer stack means the back buffer is the target, and here is where that has
    // to become true: image layout, rendering scope, and a command list of its own when nobody else
    // has one open. The draw buffer handler only sees the MOMENT a target comes or goes - an app that
    // draws on the back buffer without ever having activated one (a menu before the game starts) would
    // otherwise record nothing at all. Idempotent, and skipped while a target is active.
    if (baseRenderer.GetActiveBuffer() == nullptr)
        baseDisplayHandler.EnableBackBuffer();

    CommandList* cl = commandListHandler.CurrentCmdList();
    if (cl == nullptr)
        return false;
    VkPipeline pipeline = cl->GetPipeline(this);
    return pipeline != VK_NULL_HANDLE;
}


// The buffer each dynamic UBO binding of the draw being set up lies in - the allocator chains further
// buffers within a frame, so b0 and the b1 stages need not share one. Filled by UploadB0 / UploadB1
// and read by UpdateVariables right behind them.
static VkBuffer dynamicBuffers[Shader::kDynamicOffsetCount] { };

bool Shader::UploadB0(void) noexcept
{
    CbAlloc a = cbvAllocator.Allocate(uint32_t(sizeof(FrameConstants)));
    if (not a.IsValid())
        return false;
    std::memcpy(a.cpu, &m_b0Staging, sizeof(FrameConstants));
    m_dynamicOffsets[0] = a.offset;  // binding 0 (b0)
    dynamicBuffers[0] = a.buffer;
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
        if (sc.size == 0) {
            m_dynamicOffsets[1 + s] = 0;
            dynamicBuffers[1 + s] = dynamicBuffers[0];
            continue;
        }
        CbAlloc a = cbvAllocator.Allocate(sc.size);
        if (not a.IsValid())
            return false;
        std::memcpy(a.cpu, sc.staging.data(), sc.size);
        m_dynamicOffsets[1 + s] = a.offset;  // bindings 1 (VS), 2 (PS), 3 (GS)
        dynamicBuffers[1 + s] = a.buffer;
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

    // Worst-case write count: 4 dynamic UBOs + kSrvSlots images + kSamplerSlots samplers
    // + kUavSlots storage buffers + kSsboSlots read-only storage buffers.
    constexpr uint32_t kMaxWrites = kDynamicOffsetCount + CommandListHandler::kSrvSlots + CommandListHandler::kSamplerSlots
                                  + CommandListHandler::kUavSlots + CommandListHandler::kSsboSlots;
    VkWriteDescriptorSet writes[kMaxWrites] { };
    VkDescriptorBufferInfo bufInfos[kDynamicOffsetCount]            { };
    VkDescriptorImageInfo  imgInfos[CommandListHandler::kSrvSlots]  { };
    VkDescriptorImageInfo  smpInfos[CommandListHandler::kSamplerSlots] { };
    VkDescriptorBufferInfo stoInfos[CommandListHandler::kUavSlots]  { };
    VkDescriptorBufferInfo ssboInfos[CommandListHandler::kSsboSlots] { };
    uint32_t writeCount = 0;

    auto AddDynamicUbo = [&](uint32_t binding, uint32_t bytes, uint32_t bufSlot) {
        bufInfos[bufSlot].buffer = dynamicBuffers[bufSlot];
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
    AddDynamicUbo(kBindingB1HS,  m_stages[kStageHS].size,             4);
    AddDynamicUbo(kBindingB1DS,  m_stages[kStageDS].size,             5);

    // Sampled images (t-slots). A slot the shader declares but nobody bound gets the default image of
    // the declared view type (m_srvDefaults, reflected in UpdateStageResources ()); every other
    // unbound slot is left unwritten.
    for (uint32_t i = 0; i < CommandListHandler::kSrvSlots; ++i) {
        VkImageView v = commandListHandler.m_boundSrvViews[i];
        VkImageLayout layout = commandListHandler.m_boundSrvLayouts[i];
        if (v == VK_NULL_HANDLE) {
            v = DefaultView(m_srvDefaults[i]);
            layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        if (v == VK_NULL_HANDLE)
            continue;
        imgInfos[i].imageView   = v;
        imgInfos[i].imageLayout = layout;
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
        if ((s == VK_NULL_HANDLE) and m_samplerDeclared[i])
            s = samplerCache.GetSampler(TextureSampling { });
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
        VkBuffer b = commandListHandler.m_boundStorageBuffers[i];
        if (b == VK_NULL_HANDLE)
            continue;
        stoInfos[i].buffer = b;
        stoInfos[i].offset = 0;
        stoInfos[i].range  = commandListHandler.m_boundStorageBufferSize[i];
        VkWriteDescriptorSet& w = writes[writeCount++];
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = kUavBase + i;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo     = &stoInfos[i];
    }
    for (uint32_t i = 0; i < CommandListHandler::kSsboSlots; ++i) {
        VkBuffer b = commandListHandler.m_boundReadOnlyBuffers[i];
        if (b == VK_NULL_HANDLE)
            continue;
        ssboInfos[i].buffer = b;
        ssboInfos[i].offset = 0;
        ssboInfos[i].range  = commandListHandler.m_boundReadOnlyBufferSize[i];
        VkWriteDescriptorSet& w = writes[writeCount++];
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = set;
        w.dstBinding      = kSsboBase + i;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo     = &ssboInfos[i];
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

bool Shader::TrySetB0Field(eBaseMatrices id, const float* data) noexcept
{
    switch (id) {
        case bmModelView:
            std::memcpy(m_b0Staging.mModelView, data, 64);
            return true;
        case bmProjection:
            std::memcpy(m_b0Staging.mProjection, data, 64);
            return true;
        case bmViewport:
            std::memcpy(m_b0Staging.mViewport, data, 64);
            return true;
        case bmLightTransform:
            std::memcpy(m_b0Staging.mLightTransform, data, 64);
            return true;
        default:
            return false;
    }
}


void Shader::ResolveB1Location(ShaderLocationTable::ShaderLocation& loc, const char* name) noexcept
{
    static_assert(kStageCount == 5, "ShaderLocation::m_stageOffset assumes 5 stages (VS/PS/GS/HS/DS)");
    for (int s = 0; s < kStageCount; ++s) {
        loc.m_stageOffset[s] = -1;
        StageConstants& sc = m_stages[s];
        if (sc.size == 0)
            continue;
        for (auto& kv : sc.fields)
            if (kv.first == name) {
                loc.m_stageOffset[s] = int(kv.second.offset);
                break;
            }
    }
    loc.m_resolved = true;
}


int Shader::SetB1Field(const char* name, const void* data, size_t size) noexcept
{
    ShaderLocationTable::ShaderLocation* loc = m_locations[name];
    if (not loc)
        return -1;
    if (not loc->m_resolved)
        ResolveB1Location(*loc, name);

    int result = -1;
    for (int s = 0; s < kStageCount; ++s) {
        int offset = loc->m_stageOffset[s];
        if (offset < 0)
            continue;
        StageConstants& sc = m_stages[s];
        if (size_t(offset) + size <= sc.staging.size()) {
            std::memcpy(sc.staging.data() + offset, data, size);
            sc.dirty = true;
            if (result < 0)
                result = offset;
        }
    }
    if ((result < 0) and not loc->m_warned) {
        loc->m_warned = true;
        fprintf(stderr, "Shader '%s': unknown uniform '%s'\n", (const char*)m_name, name);
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
    return SetB1Field(name, data, 16 * sizeof(float));
}


int Shader::SetMatrix4f(eBaseMatrices id, const float* data, bool /*transpose*/) noexcept
{
    return TrySetB0Field(id, data) ? 0 : -1;
}


// A column_major float3x3 in a cbuffer keeps each column in a 16-byte slot (the last one takes 12), so
// the nine floats are spread over 44 bytes - handed over packed, the second column would start in the
// first one's padding.
int Shader::SetMatrix3f(const char* name, float* data, bool /*transpose*/) noexcept
{
    float padded[11] { };
    for (int column = 0; column < 3; ++column)
        std::memcpy(padded + column * 4, data + column * 3, 3 * sizeof(float));
    return SetB1Field(name, padded, sizeof(padded));
}


// HLSL/SPIR-V cbuffer rules pad every element of a scalar or short-vector array to a 16-byte slot.
// The Set*Array helpers therefore build a padded staging buffer (4 floats per element) before
// dispatching to SetB1Field. SetVector4fArray needs no padding (16 bytes per element already).
int Shader::SetFloatArray(const char* name, const float* data, size_t length) noexcept
{
    if (length == 0)
        return -1;
    std::vector<float> padded(length * 4, 0.0f);
    for (size_t i = 0; i < length; ++i)
        padded[i * 4] = data[i];
    return SetB1Field(name, padded.data(), length * 4 * sizeof(float));
}

// Same padding as SetFloatArray: a scalar array occupies one 16 byte slot per element.
int Shader::SetIntArray(const char* name, const int* data, size_t length) noexcept
{
    if (length == 0)
        return -1;
    std::vector<int> padded(length * 4, 0);
    for (size_t i = 0; i < length; ++i)
        padded[i * 4] = data[i];
    return SetB1Field(name, padded.data(), length * 4 * sizeof(int));
}

int Shader::SetVector2fArray(const char* name, const Vector2f* data, int length) noexcept
{
    if (length <= 0)
        return -1;
    std::vector<float> padded(size_t(length) * 4, 0.0f);
    for (int i = 0; i < length; ++i) {
        padded[size_t(i) * 4 + 0] = data[i].X();
        padded[size_t(i) * 4 + 1] = data[i].Y();
    }
    return SetB1Field(name, padded.data(), size_t(length) * 4 * sizeof(float));
}

int Shader::SetVector3fArray(const char* name, const Vector3f* data, int length) noexcept
{
    if (length <= 0)
        return -1;
    std::vector<float> padded(size_t(length) * 4, 0.0f);
    for (int i = 0; i < length; ++i) {
        padded[size_t(i) * 4 + 0] = data[i].X();
        padded[size_t(i) * 4 + 1] = data[i].Y();
        padded[size_t(i) * 4 + 2] = data[i].Z();
    }
    return SetB1Field(name, padded.data(), size_t(length) * 4 * sizeof(float));
}

int Shader::SetVector4fArray(const char* name, const Vector4f* data, int length) noexcept
{
    return SetB1Field(name, data, size_t(length) * sizeof(Vector4f));
}

// =================================================================================================
