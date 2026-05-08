#include "descriptor_pool_handler.h"

#include <cstdio>

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
        if (not CreatePool(i))
            return false;
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
}


VkDescriptorSet DescriptorPoolHandler::Allocate(VkDescriptorSetLayout layout) noexcept
{
    if ((m_device == VK_NULL_HANDLE) or (layout == VK_NULL_HANDLE))
        return VK_NULL_HANDLE;
    VkDescriptorPool pool = m_pools[m_currentFrame];
    if (pool == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult res = vkAllocateDescriptorSets(m_device, &info, &set);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "DescriptorPoolHandler::Allocate: vkAllocateDescriptorSets failed (%d) — pool exhausted?\n",
                (int)res);
        return VK_NULL_HANDLE;
    }
    return set;
}


bool DescriptorPoolHandler::CreatePool(uint32_t slot) noexcept
{
    VkDescriptorPoolSize sizes[4] { };
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    sizes[0].descriptorCount = kMaxUbosPerPool;
    sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sizes[1].descriptorCount = kMaxSampledImagesPerPool;
    sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    sizes[2].descriptorCount = kMaxSamplersPerPool;
    sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sizes[3].descriptorCount = kMaxStoragePerPool;

    VkDescriptorPoolCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = 0;  // no per-set free; pool reset only
    info.maxSets = kMaxSetsPerPool;
    info.poolSizeCount = (uint32_t)(sizeof(sizes) / sizeof(sizes[0]));
    info.pPoolSizes = sizes;

    VkResult res = vkCreateDescriptorPool(m_device, &info, nullptr, &m_pools[slot]);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "DescriptorPoolHandler::CreatePool: vkCreateDescriptorPool[%u] failed (%d)\n",
                slot, (int)res);
        return false;
    }
    return true;
}

// =================================================================================================
