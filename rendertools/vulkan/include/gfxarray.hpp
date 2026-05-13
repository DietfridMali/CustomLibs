#pragma once

#include "vkframework.h"
#include "vkcontext.h"
#include "vkupload.h"
#include "commandlist.h"
#include "shader.h"
#include "array.hpp"

#include <cstdio>
#include <cstring>

// =================================================================================================
// Vulkan GfxArray — flat R32_UINT storage buffer (analogue of the DX12 UAV-style Texture2D used by
// decalhandler::m_decalDepths). 2D indexing is folded into the shader as `y * width + x`.
//
// Storage-buffer was chosen over storage-image to:
//   • avoid the image-layout transitions GENERAL <-> TRANSFER_DST around every Clear (two
//     vkCmdPipelineBarrier2 per call),
//   • use vkCmdFillBuffer for the clear (much faster than vkCmdClearColorImage on most GPUs —
//     no layout decoding, no 2D tiling),
//   • keep the descriptor type consistent for atomic ops; SPIR-V InterlockedMin works on
//     RWStructuredBuffer<uint> just as on RWTexture2D<uint>.
//
// The clear path still needs the active render-pass scope paused (vkCmdFillBuffer is forbidden
// inside vkCmdBeginRendering exactly like vkCmdClearColorImage); the caller is responsible for
// that — see DecalHandler::Render's EndRendering/BeginRendering bracketing.
//
// Bind() writes (m_buffer, range) into the CommandListHandler bind table at the requested u-slot.
// Shader::UpdateVariables materializes the table into a VkDescriptorSet (vkUpdateDescriptorSets
// + vkCmdBindDescriptorSets) right before each draw.

class BaseGfxArray {
public:
    static inline bool IsAvailable{ true };
};


template <typename DATA_T>
class GfxArray : public BaseGfxArray
{
public:
    AutoArray<DATA_T>      m_data;

    VkBuffer               m_buffer         { VK_NULL_HANDLE };
    VmaAllocation          m_allocation     { VK_NULL_HANDLE };
    VkDeviceSize           m_bufferSize     { 0 };

    // Lazy-allocated host-visible staging buffers, kept across calls so repeat Upload/Download
    // doesn't churn allocations.
    VkBuffer               m_uploadBuffer   { VK_NULL_HANDLE };
    VmaAllocation          m_uploadAlloc    { VK_NULL_HANDLE };
    VkBuffer               m_readbackBuffer { VK_NULL_HANDLE };
    VmaAllocation          m_readbackAlloc  { VK_NULL_HANDLE };

    uint32_t               m_width          { 0 };
    uint32_t               m_height         { 0 };
    uint32_t               m_bindingPoint   { 0 };

    GfxArray() = default;

    ~GfxArray() { Destroy(); }

    inline DATA_T* Data(void) { return m_data.Data(); }
    inline int     DataSize(void) { return m_data.DataSize(); }

    bool Create(int width, int height = 1) {
        Destroy();
        m_width  = uint32_t(width);
        m_height = uint32_t(height);
        const int size = width * height;
        m_data.Resize(size);

        VmaAllocator allocator = vkContext.Allocator();
        VkDevice     device    = vkContext.Device();
        if ((allocator == VK_NULL_HANDLE) or (device == VK_NULL_HANDLE) or (size == 0))
            return false;

        m_bufferSize = VkDeviceSize(size) * VkDeviceSize(sizeof(DATA_T));

        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = m_bufferSize;
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                       | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        if (vmaCreateBuffer(allocator, &bi, &ai, &m_buffer, &m_allocation, nullptr) != VK_SUCCESS)
            return false;

        return true;
    }

    void Destroy(void) {
        VmaAllocator allocator = vkContext.Allocator();
        if (allocator != VK_NULL_HANDLE) {
            if (m_buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, m_buffer, m_allocation);
                m_buffer = VK_NULL_HANDLE;
                m_allocation = VK_NULL_HANDLE;
            }
            if (m_uploadBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, m_uploadBuffer, m_uploadAlloc);
                m_uploadBuffer = VK_NULL_HANDLE;
                m_uploadAlloc = VK_NULL_HANDLE;
            }
            if (m_readbackBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, m_readbackBuffer, m_readbackAlloc);
                m_readbackBuffer = VK_NULL_HANDLE;
                m_readbackAlloc = VK_NULL_HANDLE;
            }
        }
        m_data.Reset();
        m_width = m_height = 0;
        m_bufferSize = 0;
    }

    bool Bind(uint32_t bindingPoint) {
        if (m_buffer == VK_NULL_HANDLE)
            return false;
        m_bindingPoint = bindingPoint;
        if (bindingPoint < CommandListHandler::kUavSlots)
            commandListHandler.BindStorageBuffer(bindingPoint, m_buffer, m_bufferSize);
        return true;
    }

    void Release(uint32_t bindingPoint) {
        if (bindingPoint < CommandListHandler::kUavSlots)
            commandListHandler.BindStorageBuffer(bindingPoint, VK_NULL_HANDLE, 0);
    }

    // Writes vkCmdFillBuffer onto the currently active CommandList's CB. Contract: caller ensures
    // no vkCmdBeginRendering scope is open at call time — render-pass scopes forbid both buffer
    // fills and image clears. DecalHandler::Render handles that by calling EndRendering before
    // and BeginRendering after the clear.
    void Clear(DATA_T value) {
        if (m_buffer == VK_NULL_HANDLE)
            return;

        VkCommandBuffer cb = commandListHandler.CurrentGfxList();
        // vkCmdFillBuffer fills with a uint32 pattern repeated across the range. For DATA_T larger
        // than 4 bytes the caller would need a different fill primitive; for the decal-depths
        // case (R32_UINT, value = 0xFFFFFFFF) the pattern matches the element layout exactly.
        static_assert(sizeof(DATA_T) == sizeof(uint32_t),
                      "GfxArray::Clear assumes 32-bit elements; widen the fill path for other sizes.");
        vkCmdFillBuffer(cb, m_buffer, 0, m_bufferSize, uint32_t(value));
    }

    bool Upload(void) {
        if (m_buffer == VK_NULL_HANDLE or m_data.IsEmpty())
            return false;

        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        if (not EnsureStagingBuffer(m_uploadBuffer, m_uploadAlloc, m_bufferSize,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))
            return false;

        void* mapped = nullptr;
        if (vmaMapMemory(allocator, m_uploadAlloc, &mapped) != VK_SUCCESS)
            return false;
        std::memcpy(mapped, m_data.Data(), size_t(m_bufferSize));
        vmaUnmapMemory(allocator, m_uploadAlloc);

        OneShotCommandBuffer once;
        if (not BeginSingleTimeCommands(once))
            return false;

        VkBufferCopy region{};
        region.size = m_bufferSize;
        vkCmdCopyBuffer(once.cb, m_uploadBuffer, m_buffer, 1, &region);

        EndSingleTimeCommands(once);
        return true;
    }

    bool Download(void) {
        if (m_buffer == VK_NULL_HANDLE or m_data.IsEmpty())
            return false;

        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        if (not EnsureStagingBuffer(m_readbackBuffer, m_readbackAlloc, m_bufferSize,
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT))
            return false;

        OneShotCommandBuffer once;
        if (not BeginSingleTimeCommands(once))
            return false;

        VkBufferCopy region{};
        region.size = m_bufferSize;
        vkCmdCopyBuffer(once.cb, m_buffer, m_readbackBuffer, 1, &region);

        if (not EndSingleTimeCommands(once))
            return false;

        void* mapped = nullptr;
        if (vmaMapMemory(allocator, m_readbackAlloc, &mapped) != VK_SUCCESS)
            return false;
        std::memcpy(m_data.Data(), mapped, size_t(m_bufferSize));
        vmaUnmapMemory(allocator, m_readbackAlloc);
        return true;
    }

private:
    bool EnsureStagingBuffer(VkBuffer& outBuffer, VmaAllocation& outAlloc, VkDeviceSize bytes,
                             VkBufferUsageFlags usage, VmaAllocationCreateFlags hostFlags) noexcept
    {
        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        if (outBuffer != VK_NULL_HANDLE) {
            VmaAllocationInfo info{};
            vmaGetAllocationInfo(allocator, outAlloc, &info);
            if (info.size >= bytes)
                return true;
            vmaDestroyBuffer(allocator, outBuffer, outAlloc);
            outBuffer = VK_NULL_HANDLE;
            outAlloc  = VK_NULL_HANDLE;
        }

        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = bytes;
        bi.usage       = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = hostFlags;

        return vmaCreateBuffer(allocator, &bi, &ai, &outBuffer, &outAlloc, nullptr) == VK_SUCCESS;
    }
};

// =================================================================================================
