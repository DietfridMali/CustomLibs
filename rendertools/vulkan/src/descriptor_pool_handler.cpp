#include "descriptor_pool_handler.h"
#include "resource_handler.h"

#include <cstdio>

extern double VkStallClock(void) noexcept;
extern void VkStallEvent(const char* what, double startMs, const char* detail) noexcept;

// =================================================================================================
// DescriptorPoolHandler

bool DescriptorPoolHandler::Create(VkDevice device) noexcept
{
    if (device == VK_NULL_HANDLE) {
        fprintf(stderr, "DescriptorPoolHandler::Create: null device\n");
        return false;
    }
    m_device = device;
    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        if (not CreatePool(m_pools[i])) {
            fprintf(stderr, "DescriptorPoolHandler::Create: no pool for frame slot %u\n", i);
            return false;
        }
    }
    m_currentFrame = 0;
    return true;
}


void DescriptorPoolHandler::Destroy(void) noexcept
{
    if (m_device == VK_NULL_HANDLE)
        return;
    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        if (m_pools[i] != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_pools[i], nullptr);
            m_pools[i] = VK_NULL_HANDLE;
        }
        for (VkDescriptorPool pool : m_overflowPools[i])
            vkDestroyDescriptorPool(m_device, pool, nullptr);
        m_overflowPools[i].clear();
        m_overflowUsed[i] = 0;
    }
    m_device = VK_NULL_HANDLE;
}


void DescriptorPoolHandler::BeginFrame(uint32_t frameIndex) noexcept
{
    if (frameIndex >= FRAME_COUNT) {
        fprintf(stderr, "DescriptorPoolHandler::BeginFrame: frameIndex %u out of range (max %u)\n",
                frameIndex, FRAME_COUNT - 1);
        return;
    }
    m_currentFrame = frameIndex;
    if (m_pools[m_currentFrame] == VK_NULL_HANDLE)
        return;
    VkResult res = vkResetDescriptorPool(m_device, m_pools[m_currentFrame], 0);
    if (res != VK_SUCCESS)
        fprintf(stderr, "DescriptorPoolHandler::BeginFrame: vkResetDescriptorPool failed (%d)\n", (int)res);

    std::vector<VkDescriptorPool>& overflow = m_overflowPools[m_currentFrame];

    while (overflow.size() > m_overflowUsed[m_currentFrame]) {
        vkDestroyDescriptorPool(m_device, overflow.back(), nullptr);
        overflow.pop_back();
    }
    for (VkDescriptorPool pool : overflow) {
        res = vkResetDescriptorPool(m_device, pool, 0);
        if (res != VK_SUCCESS)
            fprintf(stderr, "DescriptorPoolHandler::BeginFrame: vkResetDescriptorPool failed (%d)\n", (int)res);
    }
    m_overflowUsed[m_currentFrame] = 0;
}


VkDescriptorPool DescriptorPoolHandler::ActivePool(void) const noexcept
{
    uint32_t used = m_overflowUsed[m_currentFrame];

    return used ? m_overflowPools[m_currentFrame][used - 1] : m_pools[m_currentFrame];
}


VkDescriptorPool DescriptorPoolHandler::NextPool(void) noexcept
{
    std::vector<VkDescriptorPool>& overflow = m_overflowPools[m_currentFrame];
    uint32_t& used = m_overflowUsed[m_currentFrame];

    if (used == overflow.size()) {
        VkDescriptorPool pool = VK_NULL_HANDLE;

        double stallStart = VkStallClock();
        if (not CreatePool(pool))
            return VK_NULL_HANDLE;
        overflow.push_back(pool);
        char detail[64];
        snprintf(detail, sizeof(detail), "overflow pool %u of frame slot %u", uint32_t(overflow.size()), m_currentFrame);
        VkStallEvent("descriptor pool create", stallStart, detail);
    }
    return overflow[used++];
}


VkDescriptorSet DescriptorPoolHandler::Allocate(VkDescriptorSetLayout layout) noexcept
{
    if ((m_device == VK_NULL_HANDLE) or (layout == VK_NULL_HANDLE))
        return VK_NULL_HANDLE;
    VkDescriptorPool pool = ActivePool();
    if (pool == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult res = vkAllocateDescriptorSets(m_device, &info, &set);
    if ((res == VK_ERROR_OUT_OF_POOL_MEMORY) or (res == VK_ERROR_FRAGMENTED_POOL)) {
        info.descriptorPool = NextPool();
        if (info.descriptorPool == VK_NULL_HANDLE)
            return VK_NULL_HANDLE;
        res = vkAllocateDescriptorSets(m_device, &info, &set);
    }
    if (res != VK_SUCCESS) {
        fprintf(stderr, "DescriptorPoolHandler::Allocate: vkAllocateDescriptorSets failed (%d)\n", (int)res);
        return VK_NULL_HANDLE;
    }
    gfxResourceHandler.NoteFrameAllocation();
    return set;
}


bool DescriptorPoolHandler::CreatePool(VkDescriptorPool& pool) noexcept
{
    VkDescriptorPoolSize sizes[5] { };
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    sizes[0].descriptorCount = kMaxUbosPerPool;
    sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sizes[1].descriptorCount = kMaxSampledImagesPerPool;
    sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    sizes[2].descriptorCount = kMaxSamplersPerPool;
    sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[3].descriptorCount = kMaxStoragePerPool;
    sizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sizes[4].descriptorCount = kMaxStorageImagesPerPool;

    VkDescriptorPoolCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = 0;  // no per-set free; pool reset only
    info.maxSets = kMaxSetsPerPool;
    info.poolSizeCount = (uint32_t)(sizeof(sizes) / sizeof(sizes[0]));
    info.pPoolSizes = sizes;

    VkResult res = vkCreateDescriptorPool(m_device, &info, nullptr, &pool);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "DescriptorPoolHandler::CreatePool: vkCreateDescriptorPool failed (%d)\n", (int)res);
        pool = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// =================================================================================================
