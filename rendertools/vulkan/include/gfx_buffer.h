#pragma once

#include "vkframework.h"

// =================================================================================================
// GfxBuffer — RAII wrapper around VkBuffer + VmaAllocation.
//
// Used by:
//   • GfxDataBuffer (vertex / index buffers, GPU-only)
//   • cbv_allocator pendant (UBO ring, persistent-mapped, host-visible)
//   • resource_handler / resource_chunkhandler (transient allocation tracking)
//   • gfxarray (storage buffer, decal-depths)
//
// VMA backing keeps memory-type selection out of the caller; Create takes a usage mask plus
// optional VmaAllocationCreateFlags (mapped / sequential-write / etc.). Destroy returns the
// allocation to VMA via the singleton vkContext.Allocator(); safe to call on an empty buffer.

class GfxBuffer
{
public:
    VkBuffer       m_buffer     { VK_NULL_HANDLE };
    VmaAllocation  m_allocation { VK_NULL_HANDLE };
    VkDeviceSize   m_size       { 0 };
    void*          m_mapped     { nullptr };  // non-null only for persistent-mapped allocations

    // Generic create. Returns false on any vmaCreateBuffer failure.
    //   size       — buffer size in bytes
    //   usage      — VkBufferUsageFlags (TRANSFER_SRC/DST | UNIFORM | VERTEX | INDEX | STORAGE | …)
    //   memUsage   — VMA memory usage hint (AUTO / AUTO_PREFER_DEVICE / AUTO_PREFER_HOST)
    //   flags      — VMA_ALLOCATION_CREATE_* flags (HOST_ACCESS_SEQUENTIAL_WRITE_BIT, MAPPED_BIT, …)
    bool Create(VkDeviceSize size, VkBufferUsageFlags usage,
                VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_AUTO,
                VmaAllocationCreateFlags flags = 0) noexcept;

    void Destroy(void) noexcept;

    // Convenience: copy bytes into a host-visible mapped buffer.
    // Returns false if the buffer is not mapped or src + offset would overflow.
    bool Upload(const void* src, VkDeviceSize bytes, VkDeviceSize offset = 0) noexcept;

    inline VkBuffer       Buffer(void)     const noexcept { return m_buffer; }
    inline VmaAllocation  Allocation(void) const noexcept { return m_allocation; }
    inline VkDeviceSize   Size(void)       const noexcept { return m_size; }
    inline void*          Mapped(void)     const noexcept { return m_mapped; }
    inline bool           IsValid(void)    const noexcept { return m_buffer != VK_NULL_HANDLE; }
};

// =================================================================================================
