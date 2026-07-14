#include "vkupload.h"
#include "vkcontext.h"
#include "image_layout_tracker.h"
#include "texture.h"
#include "gfxpixelformat_vk.h"
#include "texture_mips.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

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
// One-shot CommandBuffer setup. Allocates an ad-hoc CommandPool with TRANSIENT flag (driver hint:
// short-lived, simpler internal allocator), then one CommandBuffer. Public so other Vulkan code
// (GfxArray Clear/Upload/Download) can reuse the same blocking-submit pattern.



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

    vkCmdCopyBufferToImage(cb, outStaging.buffer, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    if (addBarrier)
        tracker.ToShaderInput(cb);

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
    bool success = true;
    for (int i = 0; i < faceCount; ++i) {
        if (not UploadSubresource(cmd.cb, dstImage, tracker, uint32_t(i), faces[i], width, height, channels, stagings[i], /*addBarrier=*/false)) {
            success = false;
            break;
        }
    }

    if (success)
        tracker.ToShaderInput(cmd.cb);

    if (not EndSingleTimeCommands(cmd))
        success = false;

    for (int i = 0; i < faceCount; ++i)
        stagings[i].Destroy();

    return success;
}

// =================================================================================================
// UploadTextureDataWithMips

// 2×2 box-filter downsample of an 8-bit, N-channel image. Odd source extents clamp the second
// tap to the last texel (same edge convention as the 3D box filter / glGenerateMipmap).
static void Downsample2D_8bit(const uint8_t* src, int sw, int sh, int channels, uint8_t* dst, int dw, int dh) noexcept
{
    for (int yd = 0; yd < dh; ++yd) {
        int ys0 = yd * 2;
        int ys1 = std::min(ys0 + 1, sh - 1);
        for (int xd = 0; xd < dw; ++xd) {
            int xs0 = xd * 2;
            int xs1 = std::min(xs0 + 1, sw - 1);
            const uint8_t* p00 = src + (size_t(ys0) * sw + xs0) * channels;
            const uint8_t* p01 = src + (size_t(ys0) * sw + xs1) * channels;
            const uint8_t* p10 = src + (size_t(ys1) * sw + xs0) * channels;
            const uint8_t* p11 = src + (size_t(ys1) * sw + xs1) * channels;
            uint8_t* o = dst + (size_t(yd) * dw + xd) * channels;
            for (int c = 0; c < channels; ++c)
                o[c] = uint8_t((int(p00[c]) + int(p01[c]) + int(p10[c]) + int(p11[c]) + 2) / 4);
        }
    }
}


bool UploadTextureDataWithMips(VkImage dstImage, ImageLayoutTracker& tracker,
                               const uint8_t* pixels, int width, int height, int channels,
                               uint32_t mipLevels) noexcept
{
    if ((dstImage == VK_NULL_HANDLE) or (pixels == nullptr))
        return false;
    if ((width <= 0) or (height <= 0) or (channels <= 0) or (mipLevels == 0))
        return false;

    // CPU-side downsample buffers for levels 1..N-1 (level 0 uploads straight from the caller's
    // pixels); one staging buffer per level, all kept alive until the one-shot submit has completed.
    AutoArray<AutoArray<uint8_t>> levels;
    AutoArray<VkStagingBuffer>    stagings;
    levels.Resize(mipLevels);
    stagings.Resize(mipLevels);

    const uint8_t* prevData = pixels;
    int prevW = width, prevH = height;
    for (uint32_t lv = 1; lv < mipLevels; ++lv) {
        int curW = std::max(1, prevW / 2);
        int curH = std::max(1, prevH / 2);
        levels[lv].Resize(uint32_t(size_t(curW) * size_t(curH) * size_t(channels)));
        Downsample2D_8bit(prevData, prevW, prevH, channels, levels[lv].Data(), curW, curH);
        prevData = levels[lv].Data();
        prevW = curW;
        prevH = curH;
    }

    bool ok = true;
    int lvW = width, lvH = height;
    for (uint32_t lv = 0; lv < mipLevels; ++lv) {
        const uint8_t* lvData = (lv == 0) ? pixels : levels[lv].Data();
        const VkDeviceSize bytes = VkDeviceSize(lvW) * VkDeviceSize(lvH) * VkDeviceSize(channels);
        if (not CreateStagingBuffer(bytes, stagings[lv])) {
            ok = false;
            break;
        }
        std::memcpy(stagings[lv].mapped, lvData, size_t(bytes));
        lvW = std::max(1, lvW / 2);
        lvH = std::max(1, lvH / 2);
    }

    OneShotCommandBuffer cmd { };
    if (ok and BeginSingleTimeCommands(cmd)) {
        // ToTransferDst barriers all mip levels (subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS).
        tracker.ToTransferDst(cmd.cb);
        lvW = width;
        lvH = height;
        for (uint32_t lv = 0; lv < mipLevels; ++lv) {
            VkBufferImageCopy copy { };
            copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel       = lv;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount     = 1;
            copy.imageOffset = { 0, 0, 0 };
            copy.imageExtent = { uint32_t(lvW), uint32_t(lvH), 1 };
            vkCmdCopyBufferToImage(cmd.cb, stagings[lv].buffer, dstImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            lvW = std::max(1, lvW / 2);
            lvH = std::max(1, lvH / 2);
        }
        tracker.ToShaderInput(cmd.cb);
        if (not EndSingleTimeCommands(cmd))
            ok = false;
    }
    else
        ok = false;

    for (uint32_t lv = 0; lv < mipLevels; ++lv)
        stagings[lv].Destroy();
    return ok;
}

// =================================================================================================
// UploadCompressedData — block-compressed (BC1/BC4/BC5/BC7) upload. One staging buffer + copy per
// (face, mip); no CPU downsampling (mips come from the DDS). Mirrors UploadTextureDataWithMips but
// uses block-sized payloads and writes each face into its own array layer.

bool UploadCompressedData(VkImage dstImage, ImageLayoutTracker& tracker,
                          const uint8_t* const* faces, int faceCount,
                          int width, int height, GfxPixelFormat fmt, int mipCount) noexcept
{
    if ((dstImage == VK_NULL_HANDLE) or (faces == nullptr) or (faceCount <= 0) or (mipCount <= 0))
        return false;
    if (faceCount > 6)
        faceCount = 6;
    const uint32_t blockBytes = GfxBlockBytes(fmt);
    if (blockBytes == 0)
        return false;   // caller passed a non-block-compressed format

    const uint32_t total = uint32_t(faceCount) * uint32_t(mipCount);
    AutoArray<VkStagingBuffer> stagings;
    stagings.Resize(total);

    // One staging buffer per (face, mip), copied straight from each face's packed DDS mip chain.
    bool     ok  = true;
    uint32_t idx = 0;
    for (int face = 0; ok and (face < faceCount); ++face) {
        const uint8_t* level = faces[face];
        int w = width, h = height;
        for (int mip = 0; mip < mipCount; ++mip) {
            const VkDeviceSize bytes = VkDeviceSize((w + 3) / 4) * VkDeviceSize((h + 3) / 4) * blockBytes;
            if (not CreateStagingBuffer(bytes, stagings[idx])) {
                ok = false;
                break;
            }
            std::memcpy(stagings[idx].mapped, level, size_t(bytes));
            level += bytes;
            ++idx;
            w = (w > 1) ? (w >> 1) : 1;
            h = (h > 1) ? (h >> 1) : 1;
        }
    }

    OneShotCommandBuffer cmd { };
    if (ok and BeginSingleTimeCommands(cmd)) {
        tracker.ToTransferDst(cmd.cb);   // barriers all mips + layers (VK_REMAINING_*)
        idx = 0;
        for (int face = 0; face < faceCount; ++face) {
            int w = width, h = height;
            for (int mip = 0; mip < mipCount; ++mip) {
                VkBufferImageCopy copy { };
                copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.mipLevel       = uint32_t(mip);
                copy.imageSubresource.baseArrayLayer = uint32_t(face);
                copy.imageSubresource.layerCount     = 1;
                copy.imageOffset = { 0, 0, 0 };
                copy.imageExtent = { uint32_t(w), uint32_t(h), 1 };
                vkCmdCopyBufferToImage(cmd.cb, stagings[idx].buffer, dstImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                ++idx;
                w = (w > 1) ? (w >> 1) : 1;
                h = (h > 1) ? (h >> 1) : 1;
            }
        }
        tracker.ToShaderInput(cmd.cb);
        if (not EndSingleTimeCommands(cmd))
            ok = false;
    }
    else
        ok = false;

    for (uint32_t i = 0; i < total; ++i)
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

    outTracker.ToShaderInput(cmd.cb);

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
// Platform-neutral upload helpers
//
// Both create the VkImage + VkImageView straight onto tex (replacing any previously owned ones)
// and call tex.SetParams(false) so the layered NoiseTexture overrides can populate m_sampling.

static void DestroyTextureGPUResources(Texture& tex) noexcept
{
    VkDevice device = vkContext.Device();
    VmaAllocator allocator = vkContext.Allocator();
    if (tex.m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, tex.m_imageView, nullptr);
        tex.m_imageView = VK_NULL_HANDLE;
    }
    if (tex.m_image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, tex.m_image, tex.m_allocation);
        tex.m_image = VK_NULL_HANDLE;
        tex.m_allocation = VK_NULL_HANDLE;
    }
}


static bool CreateView(Texture& tex, VkImageViewType viewType, VkFormat fmt,
                       uint32_t mipLevels = 1) noexcept
{
    VkDevice device = vkContext.Device();
    if ((device == VK_NULL_HANDLE) or (tex.m_image == VK_NULL_HANDLE))
        return false;

    VkImageViewCreateInfo vci { };
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = tex.m_image;
    vci.viewType = viewType;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = mipLevels;
    vci.subresourceRange.layerCount = 1;
    return vkCreateImageView(device, &vci, nullptr, &tex.m_imageView) == VK_SUCCESS;
}


bool Upload2DTexture(Texture& tex, int width, int height,
                     GfxPixelFormat fmt, const void* data) noexcept
{
    if ((data == nullptr) or (width <= 0) or (height <= 0))
        return false;

    VmaAllocator allocator = vkContext.Allocator();
    VkDevice device = vkContext.Device();
    if ((allocator == VK_NULL_HANDLE) or (device == VK_NULL_HANDLE))
        return false;

    const VkFormat vkFmt = ToVkFormat(fmt);
    const uint32_t stride = GfxPixelStride(fmt);
    if ((vkFmt == VK_FORMAT_UNDEFINED) or (stride == 0))
        return false;

    DestroyTextureGPUResources(tex);

    VkImageCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = vkFmt;
    info.extent.width = uint32_t(width);
    info.extent.height = uint32_t(height);
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator, &info, &allocInfo, &tex.m_image, &tex.m_allocation, nullptr) != VK_SUCCESS)
        return false;

    tex.m_layoutTracker.Init(tex.m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

    const uint8_t* src = reinterpret_cast<const uint8_t*>(data);
    if (not UploadTextureData(tex.m_image, tex.m_layoutTracker, src, width, height, int(stride))) {
        DestroyTextureGPUResources(tex);
        return false;
    }

    if (not CreateView(tex, VK_IMAGE_VIEW_TYPE_2D, vkFmt)) {
        DestroyTextureGPUResources(tex);
        return false;
    }

    tex.SetParams(false);
    tex.m_isValid = true;
    tex.m_isDeployed = true;
    return true;
}


bool Upload3DTexture(Texture& tex, int width, int height, int depth,
                     GfxPixelFormat fmt, const void* data,
                     bool generateMips) noexcept
{
    if ((data == nullptr) or (width <= 0) or (height <= 0) or (depth <= 0))
        return false;

    const VkFormat vkFmt = ToVkFormat(fmt);
    const uint32_t stride = GfxPixelStride(fmt);
    if ((vkFmt == VK_FORMAT_UNDEFINED) or (stride == 0))
        return false;

    VmaAllocator allocator = vkContext.Allocator();
    VkDevice device = vkContext.Device();
    if ((allocator == VK_NULL_HANDLE) or (device == VK_NULL_HANDLE))
        return false;

    // CPU-side mip chain (functional equivalent of OGL's glGenerateMipmap).
    // Only built when generateMips=true; otherwise we ship a single-mip image (Mip 0 only)
    // — same uploaded data, just no down-filtered Mip 1..N. Keeps BuildMipChain3D + the
    // multi-level upload loop intact for future opt-in callers.
    AutoArray<MipLevel3D> mipChain;
    if (generateMips)
        BuildMipChain3D(data, width, height, depth, fmt, mipChain);
    const uint32_t mipLevels = generateMips ? uint32_t(mipChain.Length()) : 1u;

    DestroyTextureGPUResources(tex);

    // Allocate VkImage with full mip pyramid.
    VkImageCreateInfo info { };
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType     = VK_IMAGE_TYPE_3D;
    info.format        = vkFmt;
    info.extent.width  = uint32_t(width);
    info.extent.height = uint32_t(height);
    info.extent.depth  = uint32_t(depth);
    info.mipLevels     = mipLevels;
    info.arrayLayers   = 1;
    info.samples       = VK_SAMPLE_COUNT_1_BIT;
    info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator, &info, &allocInfo, &tex.m_image, &tex.m_allocation, nullptr) != VK_SUCCESS)
        return false;

    tex.m_layoutTracker.Init(tex.m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

    // One staging buffer per mip level, all uploads + barriers in a single one-shot command buffer.
    // When generateMips=false, mipChain is empty — we synthesize a single-entry "chain" pointing
    // at the original input data for the Mip 0 upload below.
    AutoArray<VkStagingBuffer> stagings;
    stagings.Resize(mipLevels);
    for (uint32_t lv = 0; lv < mipLevels; ++lv) {
        const int mipW = generateMips ? mipChain[lv].width  : width;
        const int mipH = generateMips ? mipChain[lv].height : height;
        const int mipD = generateMips ? mipChain[lv].depth  : depth;
        const void* mipSrc = generateMips ? mipChain[lv].data.Data() : data;
        const VkDeviceSize bytes = VkDeviceSize(mipW) * VkDeviceSize(mipH) * VkDeviceSize(mipD) * VkDeviceSize(stride);
        if (not CreateStagingBuffer(bytes, stagings[lv])) {
            for (uint32_t i = 0; i < mipLevels; ++i)
                stagings[i].Destroy();
            DestroyTextureGPUResources(tex);
            return false;
        }
        std::memcpy(stagings[lv].mapped, mipSrc, size_t(bytes));
    }

    OneShotCommandBuffer cmd { };
    if (not BeginSingleTimeCommands(cmd)) {
        for (uint32_t i = 0; i < mipLevels; ++i)
            stagings[i].Destroy();
        DestroyTextureGPUResources(tex);
        return false;
    }

    // ToTransferDst barriers all mip levels (subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS).
    tex.m_layoutTracker.ToTransferDst(cmd.cb);

    for (uint32_t lv = 0; lv < mipLevels; ++lv) {
        const int mipW = generateMips ? mipChain[lv].width  : width;
        const int mipH = generateMips ? mipChain[lv].height : height;
        const int mipD = generateMips ? mipChain[lv].depth  : depth;
        VkBufferImageCopy copy { };
        copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel       = lv;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount     = 1;
        copy.imageOffset = { 0, 0, 0 };
        copy.imageExtent = { uint32_t(mipW), uint32_t(mipH), uint32_t(mipD) };
        vkCmdCopyBufferToImage(cmd.cb, stagings[lv].buffer, tex.m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }

    tex.m_layoutTracker.ToShaderInput(cmd.cb);

    bool ok = EndSingleTimeCommands(cmd);
    for (uint32_t i = 0; i < mipLevels; ++i)
        stagings[i].Destroy();

    if (not ok) {
        DestroyTextureGPUResources(tex);
        return false;
    }

    if (not CreateView(tex, VK_IMAGE_VIEW_TYPE_3D, vkFmt, mipLevels)) {
        DestroyTextureGPUResources(tex);
        return false;
    }

    tex.SetParams(false);
    tex.m_isValid = true;
    tex.m_isDeployed = true;
    return true;
}

// =================================================================================================
