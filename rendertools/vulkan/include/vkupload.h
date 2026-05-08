#pragma once

#include "vkframework.h"

class ImageLayoutTracker;

// =================================================================================================
// vkupload — Vulkan equivalent of dx12upload.
//
// Provides pixel-data upload helpers for VkImage textures: VMA staging buffer creation,
// vkCmdCopyBufferToImage, and image-layout transitions via ImageLayoutTracker.
//
// Submission model (Phase B): each high-level upload allocates a one-shot VkCommandBuffer
// from an ad-hoc VkCommandPool, submits + waits idle, then frees both. This is simple and
// works without the Phase-C CommandList infrastructure. When CommandList lands, callers
// can switch to passing an existing CommandBuffer via the low-level UploadSubresource entry.

struct VkStagingBuffer
{
    VkBuffer      buffer     { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    void*         mapped     { nullptr };
    VkDeviceSize  size       { 0 };

    // Destroys the staging buffer via vkContext.Allocator(). Safe to call on a default-constructed
    // (empty) buffer.
    void Destroy(void) noexcept;
};

// =================================================================================================
// Low-level: into an already-open command buffer, transition the image to TRANSFER_DST, record a
// vkCmdCopyBufferToImage from a freshly-allocated staging buffer, optionally transition to
// SHADER_READ_ONLY when addBarrier is true. outStaging is allocated by this call; caller must
// keep it alive until the command buffer has been submitted and waited, then call outStaging.Destroy().

bool UploadSubresource(VkCommandBuffer cb, VkImage dstImage, ImageLayoutTracker& tracker,
                       uint32_t layer, const uint8_t* pixels, int width, int height, int channels,
                       VkStagingBuffer& outStaging, bool addBarrier = true) noexcept;

// =================================================================================================
// High-level: opens a one-shot CommandBuffer, uploads faceCount array layers (1 for plain 2D,
// 6 for cubemaps), transitions to SHADER_READ_ONLY, submits + waits, frees temp resources.

bool UploadTextureData(VkImage dstImage, ImageLayoutTracker& tracker,
                       const uint8_t* const* faces, int faceCount,
                       int width, int height, int channels) noexcept;

// Single-subresource convenience overload.
inline bool UploadTextureData(VkImage dstImage, ImageLayoutTracker& tracker,
                              const uint8_t* pixels, int width, int height, int channels) noexcept
{
    return UploadTextureData(dstImage, tracker, &pixels, 1, width, height, channels);
}

// =================================================================================================
// Create + upload a Texture3D (VkImage with imageType=VK_IMAGE_TYPE_3D). On success outImage,
// outAllocation and outTracker hold the new image and its initial layout state (SHADER_READ_ONLY).
// Caller owns outImage/outAllocation and must vmaDestroyImage them on cleanup.

bool Upload3DTextureData(int w, int h, int d, VkFormat format, uint32_t pixelStride,
                         const void* data, VkImage& outImage, VmaAllocation& outAllocation,
                         ImageLayoutTracker& outTracker) noexcept;

// =================================================================================================
