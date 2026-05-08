#pragma once

#include <cstdint>

#include "vkframework.h"
#include "basesingleton.hpp"
#include "avltree.hpp"
#include "array.hpp"
#include "texturesampling.h"

// =================================================================================================
// SamplerCache: maps a TextureSampling configuration to a VkSampler handle.
//
// Lazy lookup — the first call to GetSampler() for a previously unseen configuration calls
// vkCreateSampler from the matching VkSamplerCreateInfo and caches the handle. Subsequent
// calls for an equal configuration return the cached handle.
//
// Cache lifetime equals the application's: samplers are never destroyed individually; Destroy()
// frees them all at app shutdown (called before VKContext::Destroy releases the device).

class SamplerCache
    : public BaseSingleton<SamplerCache>
{
public:
    using SamplerMap = AVLTree<TextureSampling, VkSampler>;


    SamplerCache(void) noexcept;


    void Destroy(void) noexcept;


    // Lazy lookup. Returns VK_NULL_HANDLE on failure (vkCreateSampler error or device missing).
    VkSampler GetSampler(const TextureSampling& s) noexcept;


private:
    SamplerMap m_cache;
    AutoArray<VkSampler> m_samplers;  // companion list for Destroy iteration


    static int Compare(void* context, const TextureSampling& a, const TextureSampling& b);


    static VkSamplerCreateInfo ToVulkanInfo(const TextureSampling& s) noexcept;
};

#define samplerCache SamplerCache::Instance()

// =================================================================================================
