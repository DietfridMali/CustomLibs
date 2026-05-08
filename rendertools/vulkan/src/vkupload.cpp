#include "vkupload.h"
#include "vkcontext.h"
#include "image_layout_tracker.h"

#include <cstdio>
#include <cstring>

// =================================================================================================
// VkStagingBuffer

void VkStagingBuffer::Destroy(void) noexcept
{
    VmaAllocator allocator = vkContext.Allocator();
    if ((buffer != VK_NULL_HANDLE) and (allocator != VK_NULL_HANDLE))
        vmaDestroyBuffer(allocator, buffer, allocation);
    buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
    mapped = nullptr;
    size = 0;
}

// =================================================================================================
// Internal: one-shot CommandBuffer setup. Allocates an ad-hoc CommandPool with TRANSIENT flag
// (driver hint: short-lived, simpler internal allocator), then one CommandBuffer.

namespace {

struct OneShotCommandBuffer
{
    VkCommandPool pool { VK_NULL_HANDLE };
    VkCommandBuffer cb { VK_NULL_HANDLE };
};


bool BeginSingleTimeCommands(OneShotCommandBuffer& out) noexcept
{
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return false;

    VkCommandPoolCreateInfo poolInfo { };
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = vkContext.GraphicsFamily();

    VkResult res = vkCreateCommandPool(device, &poolInfo, nullptr, &out.pool);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkupload::BeginSingleTimeCommands: vkCreateCommandPool failed (%d)\n", (int)res);
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo { };
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = out.pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    res = vkAllocateCommandBuffers(device, &allocInfo, &out.cb);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkupload::BeginSingleTimeCommands: vkAllocateCommandBuffers failed (%d)\n", (int)res);
        vkDestroyCommandPool(device, out.pool, nullptr);
        out.pool = VK_NULL_HANDLE;
        return false;
    }

    VkCommandBufferBeginInfo beginInfo { };
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    res = vkBeginCommandBuffer(out.cb, &beginInfo);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkupload::BeginSingleTimeCommands: vkBeginCommandBuffer failed (%d)\n", (int)res);
        vkFreeCommandBuffers(device, out.pool, 1, &out.cb);
        vkDestroyCommandPool(device, out.pool, nullptr);
        out.pool = VK_NULL_HANDLE;
        out.cb = VK_NULL_HANDLE;
        return false;
    }
    return true;
}


bool EndSingleTimeCommands(OneShotCommandBuffer& cmd) noexcept
{
    VkDevice device = vkContext.Device();
    VkQueue queue = vkContext.GraphicsQueue();
    if ((device == VK_NULL_HANDLE) or (queue == VK_NULL_HANDLE))
        return false;

    VkResult res = vkEndCommandBuffer(cmd.cb);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkupload::EndSingleTimeCommands: vkEndCommandBuffer failed (%d)\n", (int)res);
        vkFreeCommandBuffers(device, cmd.pool, 1, &cmd.cb);
        vkDestroyCommandPool(device, cmd.pool, nullptr);
        return false;
    }

    VkCommandBufferSubmitInfo cbInfo { };
    cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbInfo.commandBuffer = cmd.cb;

    VkSubmitInfo2 submit { };
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cbInfo;

    res = vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkupload::EndSingleTimeCommands: vkQueueSubmit2 failed (%d)\n", (int)res);
        vkFreeCommandBuffers(device, cmd.pool, 1, &cmd.cb);
        vkDestroyCommandPool(device, cmd.pool, nullptr);
        return false;
    }

    res = vkQueueWaitIdle(queue);
    if (res != VK_SUCCESS)
        fprintf(stderr, "vkupload::EndSingleTimeCommands: vkQueueWaitIdle failed (%d)\n", (int)res);

    vkFreeCommandBuffers(device, cmd.pool, 1, &cmd.cb);
    vkDestroyCommandPool(device, cmd.pool, nullptr);
    return true;
}


bool CreateStagingBuffer(VkDeviceSize byteSize, VkStagingBuffer& outStaging) noexcept
{
    VmaAllocator allocator = vkContext.Allocator();
    if (allocator == VK_NULL_HANDLE)
        return false;

    VkBufferCreateInfo bufInfo { };
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = byteSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResult { };
    VkResult res = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                   &outStaging.buffer, &outStaging.allocation, &allocResult);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkupload::CreateStagingBuffer: vmaCreateBuffer failed (%d)\n", (int)res);
        return false;
    }
    outStaging.mapped = allocResult.pMappedData;
    outStaging.size = byteSize;
    return true;
}

}  // namespace

// =================================================================================================
// UploadSubresource

bool UploadSubresource(VkCommandBuffer cb, VkImage dstImage, ImageLayoutTracker& tracker,
                       uint32_t layer, const uint8_t* pixels, int width, int height, int channels,
                       VkStagingBuffer& outStaging, bool addBarrier) noexcept
{
    if ((cb == VK_NULL_HANDLE) or (dstImage == VK_NULL_HANDLE) or (pixels == nullptr))
        return false;
    if ((width <= 0) or (height <= 0) or (channels <= 0))
        return false;

    const VkDeviceSize imageBytes = VkDeviceSize(width) * VkDeviceSize(height) * VkDeviceSize(channels);
    if (not CreateStagingBuffer(imageBytes, outStaging))
        return false;

    std::memcpy(outStaging.mapped, pixels, size_t(imageBytes));

    tracker.ToTransferDst(cb);

    VkBufferImageCopy copy { };
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;       // tightly packed
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = layer;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = { 0, 0, 0 };
    copy.imageExtent = { uint32_t(width), uint32_t(height), 1 };

    vkCmdCopyBufferToImage(cb, outStaging.buffer, dstImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    if (addBarrier)
        tracker.ToShaderRead(cb);

    return true;
}

// =================================================================================================
// UploadTextureData (high-level, multi-layer)

bool UploadTextureData(VkImage dstImage, ImageLayoutTracker& tracker,
                       const uint8_t* const* faces, int faceCount,
                       int width, int height, int channels) noexcept
{
    if ((dstImage == VK_NULL_HANDLE) or (faces == nullptr) or (faceCount <= 0))
        return false;
    if (faceCount > 6)
        faceCount = 6;

    OneShotCommandBuffer cmd { };
    if (not BeginSingleTimeCommands(cmd))
        return false;

    VkStagingBuffer stagings[6] { };
    bool ok = true;
    for (int i = 0; i < faceCount; ++i) {
        if (not UploadSubresource(cmd.cb, dstImage, tracker, uint32_t(i),
                                  faces[i], width, height, channels,
                                  stagings[i], /*addBarrier=*/false)) {
            ok = false;
            break;
        }
    }

    if (ok)
        tracker.ToShaderRead(cmd.cb);

    if (not EndSingleTimeCommands(cmd))
        ok = false;

    for (int i = 0; i < faceCount; ++i)
        stagings[i].Destroy();

    return ok;
}

// =================================================================================================
// Upload3DTextureData

bool Upload3DTextureData(int w, int h, int d, VkFormat format, uint32_t pixelStride,
                         const void* data, VkImage& outImage, VmaAllocation& outAllocation,
                         ImageLayoutTracker& outTracker) noexcept
{
    VmaAllocator allocator = vkContext.Allocator();
    if ((allocator == VK_NULL_HANDLE) or (data == nullptr))
        return false;
    if ((w <= 0) or (h <= 0) or (d <= 0) or (pixelStride == 0))
        return false;

    VkImageCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_3D;
    info.format = format;
    info.extent.width = uint32_t(w);
    info.extent.height = uint32_t(h);
    info.extent.depth = uint32_t(d);
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult res = vmaCreateImage(allocator, &info, &allocInfo,
                                  &outImage, &outAllocation, nullptr);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Upload3DTextureData: vmaCreateImage failed (%d)\n", (int)res);
        return false;
    }
    outTracker.Init(outImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

    const VkDeviceSize imageBytes = VkDeviceSize(w) * VkDeviceSize(h) * VkDeviceSize(d) * VkDeviceSize(pixelStride);
    VkStagingBuffer staging { };
    if (not CreateStagingBuffer(imageBytes, staging)) {
        vmaDestroyImage(allocator, outImage, outAllocation);
        outImage = VK_NULL_HANDLE;
        outAllocation = VK_NULL_HANDLE;
        return false;
    }
    std::memcpy(staging.mapped, data, size_t(imageBytes));

    OneShotCommandBuffer cmd { };
    if (not BeginSingleTimeCommands(cmd)) {
        staging.Destroy();
        vmaDestroyImage(allocator, outImage, outAllocation);
        outImage = VK_NULL_HANDLE;
        outAllocation = VK_NULL_HANDLE;
        return false;
    }

    outTracker.ToTransferDst(cmd.cb);

    VkBufferImageCopy copy { };
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = { 0, 0, 0 };
    copy.imageExtent = { uint32_t(w), uint32_t(h), uint32_t(d) };

    vkCmdCopyBufferToImage(cmd.cb, staging.buffer, outImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    outTracker.ToShaderRead(cmd.cb);

    bool ok = EndSingleTimeCommands(cmd);
    staging.Destroy();

    if (not ok) {
        vmaDestroyImage(allocator, outImage, outAllocation);
        outImage = VK_NULL_HANDLE;
        outAllocation = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// =================================================================================================
