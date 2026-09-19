#include "pipeline_cache.h"
#include "gfxpixelformat_vk.h"
#include "shader.h"
#include "vkcontext.h"
#include "shadercache.h"
#include "base_shaderhandler.h"

#include <cstdio>
#include <cstring>
#include <vector>

extern double VkStallClock(void) noexcept;
extern void VkStallEvent(const char* what, double startMs, const char* detail) noexcept;

static constexpr uint32_t kPipelineRecordVersion = 1;

// =================================================================================================
// PipelineCache

PipelineCache::PipelineCache(void) noexcept {
    m_cache.SetComparator(&PipelineCache::CompareKeys);
}


bool PipelineCache::Create(VkDevice device) noexcept
{
    if (device == VK_NULL_HANDLE)
        return false;
    m_device = device;

    VkPipelineCacheCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    // initialDataSize / pInitialData stay zero — could be loaded from disk for warm-start.

    VkResult res = vkCreatePipelineCache(device, &info, nullptr, &m_pipelineCache);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "PipelineCache::Create: vkCreatePipelineCache failed (%d)\n", (int)res);
        return false;
    }
    return true;
}


void PipelineCache::Destroy(void) noexcept
{
    if (m_device != VK_NULL_HANDLE) {
        for (auto& p : m_pipelines) {
            if (p != VK_NULL_HANDLE)
                vkDestroyPipeline(m_device, p, nullptr);
        }
        if (m_pipelineCache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
            m_pipelineCache = VK_NULL_HANDLE;
        }
    }
    m_pipelines.Reset();
    m_keys.Reset();
    m_cache.Clear();
    m_device = VK_NULL_HANDLE;
}


bool PipelineCache::Load(const String& shaderFolder)
{
    m_folder = shaderFolder;
    LoadRecords();
    if (shaderFolder.IsEmpty() or (m_pipelineCache == VK_NULL_HANDLE))
        return false;
    std::vector<uint8_t> data;
    if (not ShaderCache::ReadFile(shaderFolder, String("pipelines.vulkan"), data))
        return false;
    VkPipelineCacheHeaderVersionOne header { };
    if (data.size() < sizeof(header))
        return false;
    std::memcpy(&header, data.data(), sizeof(header));
    const VkPhysicalDeviceProperties& props = vkContext.DeviceProps();
    if ((header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
        or (header.headerSize < sizeof(header))
        or (header.vendorID != props.vendorID)
        or (header.deviceID != props.deviceID)
        or (std::memcmp(header.pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE) != 0))
        return false;

    VkPipelineCacheCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize = data.size();
    info.pInitialData = data.data();
    VkPipelineCache loaded = VK_NULL_HANDLE;
    if (vkCreatePipelineCache(m_device, &info, nullptr, &loaded) != VK_SUCCESS)
        return false;
    VkResult res = vkMergePipelineCaches(m_device, m_pipelineCache, 1, &loaded);
    vkDestroyPipelineCache(m_device, loaded, nullptr);
    return res == VK_SUCCESS;
}


bool PipelineCache::Save(void)
{
    SaveRecords();
    if (m_folder.IsEmpty() or (m_pipelineCache == VK_NULL_HANDLE))
        return false;
    size_t size = 0;
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr) != VK_SUCCESS)
        return false;
    if (size == 0)
        return false;
    std::vector<uint8_t> data(size, 0);
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, data.data()) != VK_SUCCESS)
        return false;
    return ShaderCache::WriteFile(m_folder, String("pipelines.vulkan"), data.data(), size);
}


VkPipeline PipelineCache::GetOrCreate(const PipelineKey& requestedKey) noexcept
{
    PipelineKey key = requestedKey;
    NormalizeKey(key);
    if (VkPipeline* found = m_cache.Find(key))
        return *found;

    double stallStart = VkStallClock();
    VkPipeline pipeline = BuildPipeline(key);
    if (pipeline == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    m_cache.Insert(key, pipeline);
    m_pipelines.Append(pipeline);
    m_keys.Append(key);
    Remember(key);
    char detail[256];
    snprintf(detail, sizeof(detail), "%sshader '%s', pipeline #%d, colors %u, blend %d, depth test %d write %d, stencil %d, cull %d",
             m_precreating ? "precreate " : "LAZY ", static_cast<const char*>(key.shader->m_name), int(m_pipelines.Length()),
             key.colorFormatCount, int(key.states.blendEnable[0]), int(key.states.depthTest), int(key.states.depthWrite),
             int(key.states.stencilTest), int(key.states.faceCulling));
    VkStallEvent("pipeline build", stallStart, detail);
    return pipeline;
}


void PipelineCache::NormalizeKey(PipelineKey& key) noexcept
{
    const RenderStates defaults { };
    RenderStates& s = key.states;

    s.stencilRef = defaults.stencilRef;
    s.scissorTest = defaults.scissorTest;
    if (not s.faceCulling)
        s.cullMode = defaults.cullMode;
    if (not s.depthTest) {
        s.depthWrite = 0;
        s.depthFunc = defaults.depthFunc;
    }
    if (not s.stencilTest) {
        s.stencilFunc = defaults.stencilFunc;
        s.stencilSFail = defaults.stencilSFail;
        s.stencilDPFail = defaults.stencilDPFail;
        s.stencilDPPass = defaults.stencilDPPass;
        s.stencilBackSFail = defaults.stencilBackSFail;
        s.stencilBackDPFail = defaults.stencilBackDPFail;
        s.stencilBackDPPass = defaults.stencilBackDPPass;
        s.stencilMask = defaults.stencilMask;
        s.stencilWriteMask = defaults.stencilWriteMask;
    }
    if (key.colorFormatCount <= 1)
        s.independentBlend = 0;
    int blendTargets = 0;
    if (key.colorFormatCount > 0)
        blendTargets = s.independentBlend ? int(key.colorFormatCount) : 1;
    for (int i = 0; i < RenderStates::kColorTargets; ++i) {
        bool isUsed = (i < blendTargets);
        if (not isUsed) {
            s.blendEnable[i] = defaults.blendEnable[i];
            s.colorMask[i] = defaults.colorMask[i];
        }
        if (not (isUsed and s.blendEnable[i])) {
            s.blendSrcRGB[i] = defaults.blendSrcRGB[i];
            s.blendDstRGB[i] = defaults.blendDstRGB[i];
            s.blendSrcAlpha[i] = defaults.blendSrcAlpha[i];
            s.blendDstAlpha[i] = defaults.blendDstAlpha[i];
            s.blendOpRGB[i] = defaults.blendOpRGB[i];
            s.blendOpAlpha[i] = defaults.blendOpAlpha[i];
        }
    }
}


void PipelineCache::Remember(const PipelineKey& key) noexcept
{
    PipelineRecord record { };
    record.states = key.states;
    std::memcpy(record.colorFormats, key.colorFormats, sizeof(record.colorFormats));
    record.colorFormatCount = key.colorFormatCount;
    record.depthFormat = key.depthFormat;
    for (int i = 0; i < m_records.Length(); ++i) {
        if ((m_recordNames[i] == key.shader->m_name) and (std::memcmp(&m_records[i], &record, sizeof(PipelineRecord)) == 0))
            return;
    }
    m_recordNames.Append(key.shader->m_name);
    m_records.Append(record);
    m_recordsDirty = true;
}


static uint64_t PipelineRecordFileKey(void) noexcept
{
    uint64_t key = ShaderCache::Hash(ShaderCache::kHashSeed, "pipelinekeys.vulkan");
    key = ShaderCache::Hash(key, &kPipelineRecordVersion, sizeof(kPipelineRecordVersion));
    uint64_t recordSize = uint64_t(sizeof(PipelineRecord));
    return ShaderCache::Hash(key, &recordSize, sizeof(recordSize));
}


bool PipelineCache::LoadRecords(void)
{
    m_recordNames.Reset();
    m_records.Reset();
    m_recordsDirty = false;
    if (m_folder.IsEmpty())
        return false;
    std::vector<uint8_t> payload;
    uint32_t tag = 0;
    if (not ShaderCache::Read(m_folder, String("pipelinekeys.vulkan"), PipelineRecordFileKey(), payload, tag))
        return false;
    size_t offset = 0;
    while (offset + sizeof(uint32_t) <= payload.size()) {
        uint32_t nameLength = 0;
        std::memcpy(&nameLength, payload.data() + offset, sizeof(nameLength));
        offset += sizeof(nameLength);
        if (offset + size_t(nameLength) + sizeof(PipelineRecord) > payload.size())
            break;
        String name(reinterpret_cast<const char*>(payload.data() + offset), size_t(nameLength));
        offset += size_t(nameLength);
        PipelineRecord record { };
        std::memcpy(&record, payload.data() + offset, sizeof(PipelineRecord));
        offset += sizeof(PipelineRecord);
        m_recordNames.Append(name);
        m_records.Append(record);
    }
    return true;
}


bool PipelineCache::SaveRecords(void)
{
    if (m_folder.IsEmpty() or not m_recordsDirty)
        return false;
    std::vector<uint8_t> payload;
    for (int i = 0; i < m_records.Length(); ++i) {
        const char* name = static_cast<const char*>(m_recordNames[i]);
        uint32_t nameLength = uint32_t(std::strlen(name));
        const uint8_t* lengthBytes = reinterpret_cast<const uint8_t*>(&nameLength);
        payload.insert(payload.end(), lengthBytes, lengthBytes + sizeof(nameLength));
        payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(name), reinterpret_cast<const uint8_t*>(name) + nameLength);
        const uint8_t* recordBytes = reinterpret_cast<const uint8_t*>(&m_records[i]);
        payload.insert(payload.end(), recordBytes, recordBytes + sizeof(PipelineRecord));
    }
    if (not ShaderCache::Write(m_folder, String("pipelinekeys.vulkan"), PipelineRecordFileKey(), 0, payload.data(), payload.size()))
        return false;
    m_recordsDirty = false;
    return true;
}


void PipelineCache::Precreate(void) noexcept
{
    double stallStart = VkStallClock();
    int pipelineCount = m_pipelines.Length();
    int withoutShader = 0;
    int failed = 0;
    m_precreating = true;
    for (int i = 0; i < m_records.Length(); ++i) {
        Shader* shader = baseShaderHandler.GetShader(m_recordNames[i]);
        if (shader == nullptr) {
            ++withoutShader;
            continue;
        }
        PipelineKey key { };
        key.shader = shader;
        key.states = m_records[i].states;
        std::memcpy(key.colorFormats, m_records[i].colorFormats, sizeof(key.colorFormats));
        key.colorFormatCount = m_records[i].colorFormatCount;
        key.depthFormat = m_records[i].depthFormat;
        if (GetOrCreate(key) == VK_NULL_HANDLE)
            ++failed;
    }
    m_precreating = false;
    char detail[128];
    snprintf(detail, sizeof(detail), "%d records, %d built, %d without shader, %d failed",
             int(m_records.Length()), int(m_pipelines.Length()) - pipelineCount, withoutShader, failed);
    VkStallEvent("pipeline precreate", stallStart, detail);
}


void PipelineCache::RemoveShader(Shader* shader) noexcept
{
    if (m_device == VK_NULL_HANDLE)
        return;

    // Walk the companion key list. For every entry whose shader matches, destroy the pipeline
    // and remove it from the cache. Slot stays in the parallel arrays as VK_NULL_HANDLE — it
    // is reaped at Destroy(); the cost of compacting on every RemoveShader is not worth it
    // (rare path, app shutdown / shader hot-reload).
    for (int i = 0; i < m_keys.Length(); ++i) {
        if (m_keys[i].shader != shader)
            continue;
        VkPipeline p = m_pipelines[i];
        if (p != VK_NULL_HANDLE)
            vkDestroyPipeline(m_device, p, nullptr);
        m_cache.Remove(m_keys[i]);
        m_pipelines[i] = VK_NULL_HANDLE;
        m_keys[i] = PipelineKey { };
    }
}


int PipelineCache::CompareKeys(void* /*context*/, const PipelineKey& a, const PipelineKey& b)
{
    return std::memcmp(&a, &b, sizeof(PipelineKey));
}

// =================================================================================================
// BuildPipeline — full VkGraphicsPipelineCreateInfo from RenderStates + Shader + attachment formats

VkPipeline PipelineCache::BuildPipeline(const PipelineKey& key) noexcept
{
    Shader* shader = key.shader;
    if ((not shader) or (not shader->IsValid()) or (m_device == VK_NULL_HANDLE))
        return VK_NULL_HANDLE;

    // Stages: VS + PS (+ optional GS, HS + DS).
    VkPipelineShaderStageCreateInfo stages[5] { };
    uint32_t stageCount = 0;

    stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[stageCount].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[stageCount].module = shader->m_vsModule;
    stages[stageCount].pName = "VSMain";
    ++stageCount;

    stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[stageCount].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[stageCount].module = shader->m_fsModule;
    stages[stageCount].pName = "PSMain";
    ++stageCount;

    if (shader->m_gsModule != VK_NULL_HANDLE) {
        stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stageCount].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        stages[stageCount].module = shader->m_gsModule;
        stages[stageCount].pName = "GSMain";
        ++stageCount;
    }

    const bool isTessellated = shader->IsTessellated();
    if (isTessellated) {
        stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stageCount].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        stages[stageCount].module = shader->m_hsModule;
        stages[stageCount].pName = "HSMain";
        ++stageCount;

        stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stageCount].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        stages[stageCount].module = shader->m_dsModule;
        stages[stageCount].pName = "DSMain";
        ++stageCount;
    }

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInput { };
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = uint32_t(shader->m_vsInputBindings.size());
    vertexInput.pVertexBindingDescriptions = shader->m_vsInputBindings.data();
    vertexInput.vertexAttributeDescriptionCount = uint32_t(shader->m_vsInputAttributes.size());
    vertexInput.pVertexAttributeDescriptions = shader->m_vsInputAttributes.data();

    // Input assembly: the mesh topology from RenderStates (set per draw by CommandList::SetTopology).
    const MeshTopology meshTopology = MeshTopology(key.states.topology);
    VkPipelineInputAssemblyStateCreateInfo inputAssembly { };
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    if (isTessellated)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    else if (meshTopology == MeshTopology::Lines)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    else if (meshTopology == MeshTopology::Points)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    else
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineTessellationStateCreateInfo tessellation { };
    tessellation.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    if (meshTopology == MeshTopology::Lines)
        tessellation.patchControlPoints = 2;
    else if (meshTopology == MeshTopology::Points)
        tessellation.patchControlPoints = 1;
    else
        tessellation.patchControlPoints = 3;
    if (isTessellated and (tessellation.patchControlPoints != shader->m_patchControlPoints))
        fprintf(stderr, "PipelineCache::BuildPipeline: shader '%s' expects %u patch control points, the mesh delivers %u\n",
                (const char*)shader->m_name, shader->m_patchControlPoints, tessellation.patchControlPoints);

    // Viewport / scissor: counts only — actual values set dynamically per draw.
    VkPipelineViewportStateCreateInfo viewport { };
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    // Rasterization (depth-clamp / cull / front-face / depth-bias)
    VkPipelineRasterizationStateCreateInfo rasterization { };
    key.states.SetRasterizationInfo(rasterization);

    // Multisample: 1 sample (no MSAA).
    VkPipelineMultisampleStateCreateInfo multisample { };
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth / stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil { };
    key.states.SetDepthStencilInfo(depthStencil);

    // Color blend — RT0's config applied to every color attachment, or each target's own config when
    // independent blending is requested (WBOIT: RT0 additive accum, RT1 multiplicative revealage).
    VkPipelineColorBlendAttachmentState attachments[RenderStates::kColorTargets] { };
    for (uint32_t i = 0; i < key.colorFormatCount; ++i)
        key.states.SetBlendAttachment(attachments[i], key.states.independentBlend ? int(i) : 0);
    for (uint32_t i = 0; i < key.colorFormatCount; ++i) {
        if (IsIntegerColorFormat(key.colorFormats[i]))
            attachments[i].blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend { };
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = key.colorFormatCount;
    colorBlend.pAttachments = attachments;
    colorBlend.logicOpEnable = VK_FALSE;

    // Dynamic states: viewport / scissor / stencil reference (rest pinned in the pipeline).
    static const VkDynamicState kDynamic[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    VkPipelineDynamicStateCreateInfo dynamic { };
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = uint32_t(sizeof(kDynamic) / sizeof(kDynamic[0]));
    dynamic.pDynamicStates = kDynamic;

    // Dynamic Rendering — Vulkan 1.3 Core. Replaces classic VkRenderPass binding.
    VkPipelineRenderingCreateInfo renderingInfo { };
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = key.colorFormatCount;
    renderingInfo.pColorAttachmentFormats = key.colorFormats;
    renderingInfo.depthAttachmentFormat = key.depthFormat;
    // A combined depth/stencil format has to be named on BOTH attachment slots, or the stencil test is
    // silently dead: the pipeline would carry no stencil attachment for vkCmdBeginRendering to match.
    bool hasStencilPlane = (key.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT)
                        or (key.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
                        or (key.depthFormat == VK_FORMAT_D16_UNORM_S8_UINT);
    renderingInfo.stencilAttachmentFormat = hasStencilPlane ? key.depthFormat : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &renderingInfo;
    info.stageCount = stageCount;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pTessellationState = isTessellated ? &tessellation : nullptr;
    info.pViewportState = &viewport;
    info.pRasterizationState = &rasterization;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamic;
    info.layout = shader->m_pipelineLayout;
    info.renderPass = VK_NULL_HANDLE;  // dynamic rendering — no render pass
    info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult res = vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &info, nullptr, &pipeline);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "PipelineCache::BuildPipeline: vkCreateGraphicsPipelines failed (%d)\n", (int)res);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

// =================================================================================================
