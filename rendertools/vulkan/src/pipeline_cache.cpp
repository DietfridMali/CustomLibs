#include "pipeline_cache.h"
#include "shader.h"
#include "vkcontext.h"

#include <cstdio>
#include <cstring>

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


VkPipeline PipelineCache::GetOrCreate(const PipelineKey& key) noexcept
{
    if (VkPipeline* found = m_cache.Find(key))
        return *found;

    VkPipeline pipeline = BuildPipeline(key);
    if (pipeline == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    m_cache.Insert(key, pipeline);
    m_pipelines.Append(pipeline);
    m_keys.Append(key);
    return pipeline;
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

    // Stages: VS + PS (+ optional GS).
    VkPipelineShaderStageCreateInfo stages[3] { };
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

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInput { };
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = uint32_t(shader->m_vsInputBindings.size());
    vertexInput.pVertexBindingDescriptions = shader->m_vsInputBindings.data();
    vertexInput.vertexAttributeDescriptionCount = uint32_t(shader->m_vsInputAttributes.size());
    vertexInput.pVertexAttributeDescriptions = shader->m_vsInputAttributes.data();

    // Input assembly: triangle list (matches DX12 PrimitiveTopologyType_TRIANGLE).
    VkPipelineInputAssemblyStateCreateInfo inputAssembly { };
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

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

    // Color blend — same RenderStates blend config applied to every color attachment.
    VkPipelineColorBlendAttachmentState attachments[8] { };
    for (uint32_t i = 0; i < key.colorFormatCount; ++i)
        key.states.SetBlendAttachment(attachments[i]);

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
    renderingInfo.stencilAttachmentFormat = (key.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT)
                                          ? key.depthFormat
                                          : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &renderingInfo;
    info.stageCount = stageCount;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
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
