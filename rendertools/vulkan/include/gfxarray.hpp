#pragma once

#include "vkframework.h"
#include "vkcontext.h"
#include "vkupload.h"
#include "image_layout_tracker.h"
#include "commandlist.h"
#include "shader.h"
#include "array.hpp"

#include <cstdio>
#include <cstring>

// =================================================================================================
// Vulkan GfxArray — 2D R32_UINT storage image (analogue of the DX12 UAV-style Texture2D used by
// decalhandler::m_decalDepths).
//
// Storage image lives in VK_IMAGE_LAYOUT_GENERAL between operations (the layout that allows
// shader storage-image read/write). Clear / Upload / Download briefly transition to
// TRANSFER_DST / TRANSFER_SRC and back. They run on a one-shot CommandBuffer (blocking submit),
// not on the active per-frame CommandList — Phase B doesn't yet expose a recording CB to high-
// level callers, and the DX12 path's frame-level batching can be revisited once Phase C lands.
//
// Bind() is currently a tracking-only stub: it records (binding, image-view) so a later
// vkCmdBindDescriptorSets call (Phase C) can materialize the storage-image into the active
// VkDescriptorSet at slot u(bindingPoint).

class BaseGfxArray {
public:
    static inline bool IsAvailable{ true };
};


template <typename DATA_T>
class GfxArray : public BaseGfxArray
{
public:
    AutoArray<DATA_T>      m_data;

    VkImage                m_image          { VK_NULL_HANDLE };
    VkImageView            m_imageView      { VK_NULL_HANDLE };
    VmaAllocation          m_allocation     { VK_NULL_HANDLE };
    ImageLayoutTracker     m_layoutTracker;

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

        VkImageCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R32_UINT;
        info.extent.width  = m_width;
        info.extent.height = m_height;
        info.extent.depth  = 1;
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = VK_IMAGE_USAGE_STORAGE_BIT
                           | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                           | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        if (vmaCreateImage(allocator, &info, &allocInfo, &m_image, &m_allocation, nullptr) != VK_SUCCESS)
            return false;

        m_layoutTracker.Init(m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageViewCreateInfo vci{};
        vci.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                       = m_image;
        vci.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                      = VK_FORMAT_R32_UINT;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, nullptr, &m_imageView) != VK_SUCCESS)
            return false;

        // Bring the image to GENERAL on a one-shot CB so the first Bind/Clear has a stable
        // baseline layout; subsequent Clear/Upload/Download will transition as needed.
        OneShotCommandBuffer once;
        if (BeginSingleTimeCommands(once)) {
            m_layoutTracker.ToGeneral(once.cb);
            EndSingleTimeCommands(once);
        }

        return true;
    }

    void Destroy(void) {
        VmaAllocator allocator = vkContext.Allocator();
        VkDevice     device    = vkContext.Device();
        if (device != VK_NULL_HANDLE and m_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }
        if (allocator != VK_NULL_HANDLE) {
            if (m_image != VK_NULL_HANDLE) {
                vmaDestroyImage(allocator, m_image, m_allocation);
                m_image = VK_NULL_HANDLE;
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
    }

    bool Bind(uint32_t bindingPoint) {
        if (m_image == VK_NULL_HANDLE)
            return false;
        m_bindingPoint = bindingPoint;
        // Phase C: write (m_imageView, VK_IMAGE_LAYOUT_GENERAL) into the per-frame
        // CommandListHandler bind table at u-slot bindingPoint; mark dirty so the next Draw
        // materializes a VkDescriptorSet write (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) and
        // vkCmdBindDescriptorSets. The DX12 SetGraphicsRootDescriptorTable equivalent.
        return true;
    }

    void Release(uint32_t /*bindingPoint*/) {
        // Phase C: clear storage-image entry at u-slot bindingPoint in the bind table.
    }

    void Clear(DATA_T value) {
        if (m_image == VK_NULL_HANDLE)
            return;

        OneShotCommandBuffer once;
        if (not BeginSingleTimeCommands(once))
            return;

        m_layoutTracker.ToTransferDst(once.cb);

        VkClearColorValue cv{};
        // R32_UINT is treated as the .uint32 channel; populate all 4 entries even though only
        // .uint32[0] is read for single-channel formats — Vulkan validation expects the union to
        // be initialized.
        cv.uint32[0] = uint32_t(value);
        cv.uint32[1] = uint32_t(value);
        cv.uint32[2] = uint32_t(value);
        cv.uint32[3] = uint32_t(value);

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(once.cb, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &range);

        m_layoutTracker.ToGeneral(once.cb);
        EndSingleTimeCommands(once);
    }

    bool Upload(void) {
        if (m_image == VK_NULL_HANDLE or m_data.IsEmpty())
            return false;

        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        const VkDeviceSize bytes = VkDeviceSize(m_width) * VkDeviceSize(m_height) * VkDeviceSize(sizeof(DATA_T));
        if (not EnsureStagingBuffer(m_uploadBuffer, m_uploadAlloc, bytes,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))
            return false;

        // Map + copy + unmap (persistent mapping not requested for upload — we only access on Upload).
        void* mapped = nullptr;
        if (vmaMapMemory(allocator, m_uploadAlloc, &mapped) != VK_SUCCESS)
            return false;
        std::memcpy(mapped, m_data.Data(), size_t(bytes));
        vmaUnmapMemory(allocator, m_uploadAlloc);

        OneShotCommandBuffer once;
        if (not BeginSingleTimeCommands(once))
            return false;

        m_layoutTracker.ToTransferDst(once.cb);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width  = m_width;
        region.imageExtent.height = m_height;
        region.imageExtent.depth  = 1;
        vkCmdCopyBufferToImage(once.cb, m_uploadBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        m_layoutTracker.ToGeneral(once.cb);
        EndSingleTimeCommands(once);
        return true;
    }

    bool Download(void) {
        if (m_image == VK_NULL_HANDLE or m_data.IsEmpty())
            return false;

        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        const VkDeviceSize bytes = VkDeviceSize(m_width) * VkDeviceSize(m_height) * VkDeviceSize(sizeof(DATA_T));
        if (not EnsureStagingBuffer(m_readbackBuffer, m_readbackAlloc, bytes,
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT))
            return false;

        OneShotCommandBuffer once;
        if (not BeginSingleTimeCommands(once))
            return false;

        m_layoutTracker.ToTransferSrc(once.cb);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width  = m_width;
        region.imageExtent.height = m_height;
        region.imageExtent.depth  = 1;
        vkCmdCopyImageToBuffer(once.cb, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readbackBuffer, 1, &region);

        m_layoutTracker.ToGeneral(once.cb);
        if (not EndSingleTimeCommands(once))
            return false;

        void* mapped = nullptr;
        if (vmaMapMemory(allocator, m_readbackAlloc, &mapped) != VK_SUCCESS)
            return false;
        std::memcpy(m_data.Data(), mapped, size_t(bytes));
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

        // Re-allocate if a previous buffer was sized for a smaller image (size change between
        // calls would otherwise cause out-of-bounds copies).
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
