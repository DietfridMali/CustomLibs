#include "gfx_buffer.h"
#include "vkcontext.h"

#include <cstdio>
#include <cstring>

// =================================================================================================
// GfxBuffer

bool GfxBuffer::Create(VkDeviceSize size, VkBufferUsageFlags usage,
                       VmaMemoryUsage memUsage, VmaAllocationCreateFlags flags) noexcept
{
    Destroy();

    VmaAllocator allocator = vkContext.Allocator();
    if ((allocator == VK_NULL_HANDLE) or (size == 0))
        return false;

    VkBufferCreateInfo bufInfo { };
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = memUsage;
    allocInfo.flags = flags;

    VmaAllocationInfo allocResult { };
    VkResult res = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                   &m_buffer, &m_allocation, &allocResult);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "GfxBuffer::Create: vmaCreateBuffer failed (%d, size=%llu)\n",
                (int)res, (unsigned long long)size);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        return false;
    }
    m_size = size;
    m_mapped = (flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) ? allocResult.pMappedData : nullptr;
    return true;
}


void GfxBuffer::Destroy(void) noexcept
{
    VmaAllocator allocator = vkContext.Allocator();
    if ((m_buffer != VK_NULL_HANDLE) and (allocator != VK_NULL_HANDLE))
        vmaDestroyBuffer(allocator, m_buffer, m_allocation);
    m_buffer = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_size = 0;
    m_mapped = nullptr;
}


bool GfxBuffer::Upload(const void* src, VkDeviceSize bytes, VkDeviceSize offset) noexcept
{
    if (m_mapped == nullptr)
        return false;
    if ((src == nullptr) or (bytes == 0))
        return false;
    if (offset + bytes > m_size)
        return false;
    std::memcpy(static_cast<uint8_t*>(m_mapped) + offset, src, size_t(bytes));
    return true;
}

// =================================================================================================
