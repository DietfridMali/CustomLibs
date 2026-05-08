#pragma once

#include "vkframework.h"
#include "basesingleton.hpp"

// =================================================================================================
// DescriptorPoolHandler — Vulkan equivalent of the DX12 DescriptorHeapHandler.
//
// Holds one VkDescriptorPool per frame slot. At the start of each frame slot the corresponding
// pool is reset (vkResetDescriptorPool) which invalidates all descriptor sets allocated from it
// in the previous cycle — no per-set free needed.
//
// Allocate(layout) hands out a VkDescriptorSet from the current frame slot's pool. The set is
// owned by the pool and is good only until the next BeginFrame on the same slot.
//
// Pool size budget per frame slot. Numbers are upper bounds for one frame's worth of
// shader-activates; tunable when profiling shows actual usage:
//   kMaxSetsPerPool      — how many distinct VkDescriptorSet objects per frame
//   kMaxUbosPerPool      — UNIFORM_BUFFER_DYNAMIC slot count (b0/b1 per stage)
//   kMaxImagesPerPool    — COMBINED_IMAGE_SAMPLER slot count (t0..t15 per draw)
//   kMaxStoragePerPool   — STORAGE_IMAGE slot count (u0..u3 per draw)
//
// Singleton, created from Application::InitGraphics() after VKContext is up, before BeginFrame.

class DescriptorPoolHandler : public BaseSingleton<DescriptorPoolHandler>
{
public:
    static constexpr uint32_t FRAME_COUNT = 2;
    static constexpr uint32_t kMaxSetsPerPool = 1024;
    static constexpr uint32_t kMaxUbosPerPool = 4096;
    static constexpr uint32_t kMaxSampledImagesPerPool = 8192;  // t0..t15 per draw
    static constexpr uint32_t kMaxSamplersPerPool = 8192;       // s0..s15 per draw
    static constexpr uint32_t kMaxStoragePerPool = 256;         // u0..u3 per draw

    VkDevice         m_device       { VK_NULL_HANDLE };
    VkDescriptorPool m_pools[FRAME_COUNT] { };
    uint32_t         m_currentFrame { 0 };

    // Allocates one VkDescriptorPool per frame slot. Returns false on any vkCreateDescriptorPool failure.
    bool Create(VkDevice device) noexcept;

    void Destroy(void) noexcept;

    // Called from CommandQueue::BeginFrame after fence-wait. Resets the current slot's pool so
    // all sets allocated last cycle on this slot become invalid.
    void BeginFrame(uint32_t frameIndex) noexcept;

    // Allocates one VkDescriptorSet with the given layout from the current slot's pool.
    // Returns VK_NULL_HANDLE on pool exhaustion (caller should log + skip the activate).
    VkDescriptorSet Allocate(VkDescriptorSetLayout layout) noexcept;

    inline VkDescriptorPool CurrentPool(void) const noexcept { return m_pools[m_currentFrame]; }

private:
    bool CreatePool(uint32_t slot) noexcept;
};

#define descriptorPoolHandler DescriptorPoolHandler::Instance()

// =================================================================================================
