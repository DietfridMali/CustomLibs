#pragma once

#include <cstdint>

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "avltree.hpp"
#include "texturesampling.h"

// =================================================================================================
// SamplerCache: maps a TextureSampling configuration to a sampler-heap slot index.
//
// Lazy lookup — the first call to GetSlot() for a previously unseen configuration
// allocates a slot in DescriptorHeapHandler::m_samplerHeap, builds the matching
// D3D12_SAMPLER_DESC, calls device->CreateSampler() and stores the slot in the
// cache. Subsequent calls for an equal configuration return the cached slot.
//
// Cache lifetime equals the application's: slots are never freed individually;
// the underlying sampler-heap is destroyed when DescriptorHeapHandler shuts down.

class SamplerCache
    : public BaseSingleton<SamplerCache>
{
public:
    using SamplerMap = AVLTree<TextureSampling, uint32_t>;


    SamplerCache(void) noexcept;


    void Destroy(void) noexcept;


    // Lazy lookup. Returns UINT32_MAX on failure (heap full or device missing).
    uint32_t GetSlot(const TextureSampling& s) noexcept;


private:
    SamplerMap m_cache;


    static int Compare(void* context, const TextureSampling& a, const TextureSampling& b);


    static D3D12_SAMPLER_DESC ToD3D12Desc(const TextureSampling& s) noexcept;
};

#define samplerCache SamplerCache::Instance()

// =================================================================================================
