#pragma once

#ifndef _TEXTURE_H
#   include "texture.h"
#endif

#include "vkcontext.h"
#include "vkupload.h"
#include <cstdint>
#include <cstring>
#include <algorithm>

// =================================================================================================
// Vulkan LinearTexture — 1D data stored as a Texture2D with height=1.
// Replaces the OGL version (glTexImage2D / glTexSubImage2D).

template<typename T> struct GfxTexTraits;

template<> struct GfxTexTraits<uint8_t> {
    static constexpr VkFormat format    = VK_FORMAT_R8_UNORM;
    static constexpr uint32_t pixelSize = 1;
    static constexpr uint32_t channels  = 1;
};

template<> struct GfxTexTraits<Vector4f> {
    static constexpr VkFormat format    = VK_FORMAT_R32G32B32A32_SFLOAT;
    static constexpr uint32_t pixelSize = 16;
    static constexpr uint32_t channels  = 4;
};

// =================================================================================================

template <typename DATA_T>
class LinearTexture : public Texture
{
public:
    AutoArray<DATA_T>   m_data;
    bool                m_isRepeating{ false };

    LinearTexture() = default;
    ~LinearTexture() = default;

    void SetParams(bool /*enforce*/) override
    {
        m_hasParams = true;
        m_sampling.minFilter = GfxFilterMode::Nearest;
        m_sampling.magFilter = GfxFilterMode::Nearest;
        m_sampling.mipMode = GfxMipMode::None;
        m_sampling.wrapU = m_isRepeating ? GfxWrapMode::Repeat : GfxWrapMode::ClampToEdge;
        m_sampling.wrapV = GfxWrapMode::ClampToEdge;
        m_sampling.wrapW = GfxWrapMode::ClampToEdge;
        m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
        m_sampling.maxAnisotropy = 1.0f;
    }


    virtual bool Deploy(int /*bufferIndex*/) override {
        if (m_buffers.IsEmpty())
            return false;
        TextureBuffer* tb = m_buffers[0];
        int width = tb->m_info.m_width;
        if (width <= 0)
            return false;

        VmaAllocator allocator = vkContext.Allocator();
        VkDevice device = vkContext.Device();
        if ((allocator == VK_NULL_HANDLE) or (device == VK_NULL_HANDLE))
            return false;

        // (Re-)create a 2D image with height=1 and the trait-specific format.
        if (m_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }
        if (m_image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, m_image, m_allocation);
            m_image = VK_NULL_HANDLE;
            m_allocation = VK_NULL_HANDLE;
        }

        VkImageCreateInfo info { };
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = GfxTexTraits<DATA_T>::format;
        info.extent.width = uint32_t(width);
        info.extent.height = 1;
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
        if (vmaCreateImage(allocator, &info, &allocInfo, &m_image, &m_allocation, nullptr) != VK_SUCCESS)
            return false;

        m_layoutTracker.Init(m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);

        const uint8_t* src = static_cast<const uint8_t*>(tb->DataBuffer());
        if (not UploadTextureData(m_image, m_layoutTracker, src, width, 1, int(GfxTexTraits<DATA_T>::pixelSize)))
            return false;

        VkImageViewCreateInfo vci { };
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = m_image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = GfxTexTraits<DATA_T>::format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, nullptr, &m_imageView) != VK_SUCCESS)
            return false;

        m_isDeployed = true;
        return true;
    }


    inline bool Create(AutoArray<DATA_T>& data, bool isRepeating) {
        if (not Texture::Create())
            return false;
        m_isRepeating = isRepeating;
        if (not Allocate(static_cast<int>(data.Length())))
            return false;
        Upload(data);
        return true;
    }


    bool Allocate(int length) {
        TextureBuffer* tb = new (std::nothrow) TextureBuffer();
        if (not tb)
            return false;
        if (not m_buffers.Append(tb)) {
            delete tb;
            return false;
        }
        tb->m_info = TextureBuffer::BufferInfo(
            length, 1,
            int(GfxTexTraits<DATA_T>::channels),
            0, 0);
        tb->m_data.Resize(length * int(GfxTexTraits<DATA_T>::pixelSize));
        return Deploy(0);
    }


    inline int Upload(AutoArray<DATA_T>& data) {
        if (m_buffers.IsEmpty()) return 0;
        TextureBuffer* tb = m_buffers[0];
        const int l = std::min(GetWidth(), int(data.Length()));
        if (l <= 0) return 0;

        std::memcpy(tb->DataBuffer(), data.Data(), l * int(GfxTexTraits<DATA_T>::pixelSize));

        Deploy(0);
        return l;
    }
};

// =================================================================================================
