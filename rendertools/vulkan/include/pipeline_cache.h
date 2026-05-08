#pragma once

#include "vkframework.h"
#include "basesingleton.hpp"
#include "avltree.hpp"
#include "array.hpp"
#include "renderstates.h"

class Shader;

// =================================================================================================
// PipelineCache — Vulkan replacement for the DX12 PSO class.
//
// Caches VkPipeline objects keyed by {Shader*, RenderStates, color/depth attachment formats}.
// First lookup builds a fresh pipeline via vkCreateGraphicsPipelines using a VkPipelineCache
// (driver-side cache, accelerates re-builds across program runs if persisted to disk).
//
// The DX12 PSO key was {Shader*, RenderStates}: format information was implicit because PSOs
// store render-target formats inline. In Vulkan with dynamic_rendering the format set is
// passed via VkPipelineRenderingCreateInfo at pipeline build, so it must be part of the key
// (a single shader rendered into different RT formats needs distinct pipelines).
//
// Cache lifetime equals the application's. Destroy() frees all pipelines.

#pragma pack(push, 1)
struct PipelineKey {
    Shader*       shader            { nullptr };
    RenderStates  states;
    VkFormat      colorFormats[8]   { };
    uint32_t      colorFormatCount  { 0 };
    VkFormat      depthFormat       { VK_FORMAT_UNDEFINED };
};
#pragma pack(pop)


class PipelineCache : public BaseSingleton<PipelineCache>
{
public:
    using Cache = AVLTree<PipelineKey, VkPipeline>;

    VkDevice         m_device         { VK_NULL_HANDLE };
    VkPipelineCache  m_pipelineCache  { VK_NULL_HANDLE };
    Cache            m_cache;

    // Companion lists kept in lock-step. Used for Destroy iteration and RemoveShader sweep.
    AutoArray<VkPipeline>   m_pipelines;
    AutoArray<PipelineKey>  m_keys;

    PipelineCache(void) noexcept;

    bool Create(VkDevice device) noexcept;
    void Destroy(void) noexcept;

    // Cache lookup. On miss, builds the pipeline via vkCreateGraphicsPipelines and inserts it.
    // Returns VK_NULL_HANDLE on shader/build failure.
    VkPipeline GetOrCreate(const PipelineKey& key) noexcept;

    // Removes (and destroys) every cached pipeline that belongs to the given shader.
    // Called from Shader::Destroy.
    void RemoveShader(Shader* shader) noexcept;

private:
    static int CompareKeys(void* context, const PipelineKey& a, const PipelineKey& b);

    VkPipeline BuildPipeline(const PipelineKey& key) noexcept;
};

#define pipelineCache PipelineCache::Instance()

// =================================================================================================
