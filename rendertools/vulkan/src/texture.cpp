#define NOMINMAX

#include <utility>
#include <stdio.h>

#include "std_defines.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL_image.h"
#pragma warning(pop)

#include "texture.h"
#include "vkcontext.h"
#include "vkupload.h"
#include "commandlist.h"
#include "sampler_cache.h"
#include "gfxstates.h"

// =================================================================================================
// Vulkan Texture implementation
//
// Members: VkImage / VkImageView / VmaAllocation / ImageLayoutTracker.
// Create / Destroy / CreateTextureResource / CreateSRV use VMA + vkCreateImageView; Deploy
// routes through vkupload helpers. Bind / Release stage (image-view, sampler) into the
// CommandListHandler bind table; Shader::UpdateVariables materializes the table into a
// VkDescriptorSet (vkUpdateDescriptorSets + vkCmdBindDescriptorSets) right before each draw.
// Common-logic methods (Load, CreateFromFile, CreateFromSurface, Cartoonize, ComputeOffsets,
// SetParams, SetWrapping) are API-neutral and unchanged from the DX12 version.

uint32_t Texture::nullHandle = UINT32_MAX;

int Texture::CompareTextures(void* context, const String& k1, const String& k2) {
    int i = String::Compare(nullptr, k1, k2);
    return (i < 0) ? -1 : (i > 0) ? 1 : 0;
}

// =================================================================================================

Texture::Texture(uint32_t handle, TextureType type, GfxWrapMode wrap)
    : m_handle(handle)
    , m_type(type)
    , m_tmuIndex(-1)
    , m_wrapMode(wrap)
    , m_name("")
{
    SetupLUT();
}


Texture::~Texture() noexcept
{
    if (m_isValid) {
        if (UpdateLUT()) {
            textureLUT.Remove(m_name);
            m_name = "";
        }
        Destroy();
    }
}


Texture& Texture::Copy(const Texture& other)
{
    if (this != &other) {
        Destroy();
        m_handle = other.m_handle;
        // Vulkan has no refcounted ComPtr — we do not share VkImage / VkImageView / VmaAllocation.
        // After Copy the new Texture has no GPU image; caller is expected to Deploy again.
        m_image = VK_NULL_HANDLE;
        m_imageView = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_layoutTracker = ImageLayoutTracker { };
        m_name = other.m_name;
        m_buffers = other.m_buffers;
        m_filenames = other.m_filenames;
        m_type = other.m_type;
        m_wrapMode = other.m_wrapMode;
        m_useMipMaps = other.m_useMipMaps;
        m_isDeployed = false;
        m_hasParams = other.m_hasParams;
        m_isValid = other.m_isValid;
    }
    return *this;
}


Texture& Texture::Move(Texture& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_handle = std::exchange(other.m_handle, UINT32_MAX);
        m_image = std::exchange(other.m_image, VkImage(VK_NULL_HANDLE));
        m_imageView = std::exchange(other.m_imageView, VkImageView(VK_NULL_HANDLE));
        m_allocation = std::exchange(other.m_allocation, VmaAllocation(VK_NULL_HANDLE));
        m_layoutTracker = other.m_layoutTracker;
        other.m_layoutTracker = ImageLayoutTracker { };
        m_name = std::move(other.m_name);
        m_buffers = std::move(other.m_buffers);
        m_filenames = std::move(other.m_filenames);
        m_type = other.m_type;
        m_wrapMode = other.m_wrapMode;
        m_useMipMaps = other.m_useMipMaps;
        m_isDeployed = std::exchange(other.m_isDeployed, false);
        m_hasParams = other.m_hasParams;
        m_isValid = std::exchange(other.m_isValid, false);
    }
    return *this;
}


bool Texture::Create(void)
{
    Destroy();
    // Vulkan: no GPU descriptor-heap slot to allocate. The image itself is created in
    // CreateTextureResource (called from Deploy with the actual pixel dimensions).
    m_isValid = true;
    return true;
}


void Texture::Destroy(void)
{
    if (not m_isValid)
        return;
    m_isValid = false;
    m_isDeployed = false;

    VkDevice device = vkContext.Device();
    VmaAllocator allocator = vkContext.Allocator();

    if ((m_imageView != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE)) {
        vkDestroyImageView(device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if ((m_image != VK_NULL_HANDLE) and (allocator != VK_NULL_HANDLE)) {
        vmaDestroyImage(allocator, m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
    m_layoutTracker = ImageLayoutTracker { };
    m_handle = UINT32_MAX;

    for (auto* p : m_buffers) {
        if (p->m_refCount) --p->m_refCount;
        else delete p;
    }
    m_buffers.Clear();
}


bool Texture::IsAvailable(void)
{
    if (not m_isValid)
        return false;
    if (m_isDeployed)
        return true;
    return false;
}


bool Texture::Bind(int tmuIndex)
{
    if (not IsAvailable())
        return false;
    m_tmuIndex = tmuIndex;

    // Lazily populate the per-texture sampler configuration on first bind.
    if (not m_hasParams)
        SetParams(false);

    // Stage the (image-view, sampler) pair in the CommandListHandler bind table at slot
    // tmuIndex. Materialized into a VkDescriptorSet by Shader::UpdateVariables right before the
    // next draw (descriptorPoolHandler.Allocate + vkUpdateDescriptorSets + vkCmdBindDescriptorSets).
    // The DX12 SetGraphicsRootDescriptorTable equivalent.
    if (tmuIndex >= 0 and uint32_t(tmuIndex) < CommandListHandler::kSrvSlots) {
        commandListHandler.BindSampledImage(uint32_t(tmuIndex), m_imageView);
        VkSampler sampler = samplerCache.GetSampler(m_sampling);
        commandListHandler.BindSampler(uint32_t(tmuIndex), sampler);
    }
    return true;
}


void Texture::Release(void)
{
    if (m_tmuIndex >= 0 and uint32_t(m_tmuIndex) < CommandListHandler::kSrvSlots) {
        commandListHandler.BindSampledImage(uint32_t(m_tmuIndex), VK_NULL_HANDLE);
        commandListHandler.BindSampler(uint32_t(m_tmuIndex), VK_NULL_HANDLE);
    }
    m_tmuIndex = -1;
}


void Texture::SetParams(bool forceUpdate)
{
    if (not (forceUpdate or not m_hasParams))
        return;
    m_hasParams = true;

    // Default: linear filter, repeat wrap (most textures in this app are tile/wrap-style).
    // Subclasses that need clamp (RenderTargetTexture, ShadowTexture, Cubemap) override this.
    m_sampling.minFilter = GfxFilterMode::Linear;
    m_sampling.magFilter = GfxFilterMode::Linear;
    m_sampling.mipMode = m_useMipMaps ? GfxMipMode::Linear : GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


bool Texture::CreateTextureResource(int w, int h, int arraySize)
{
    VmaAllocator allocator = vkContext.Allocator();
    if (allocator == VK_NULL_HANDLE)
        return false;

    VkImageCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent.width = uint32_t(w);
    info.extent.height = uint32_t(h);
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = uint32_t(arraySize);
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if ((m_type == TextureType::CubeMap) and (arraySize == 6))
        info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult res = vmaCreateImage(allocator, &info, &allocInfo, &m_image, &m_allocation, nullptr);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Texture::CreateTextureResource: vmaCreateImage failed (%d)\n", (int)res);
        return false;
    }

    m_layoutTracker.Init(m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);
    return true;
}


bool Texture::CreateSRV(void)
{
    VkDevice device = vkContext.Device();
    if ((device == VK_NULL_HANDLE) or (m_image == VK_NULL_HANDLE))
        return false;

    VkImageViewCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = m_image;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.viewType = (m_type == TextureType::CubeMap) ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkResult res = vkCreateImageView(device, &info, nullptr, &m_imageView);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Texture::CreateSRV: vkCreateImageView failed (%d)\n", (int)res);
        return false;
    }
    return true;
}


bool Texture::Deploy(int bufferIndex)
{
    if (m_isDeployed)
        return true;
    if (bufferIndex >= m_buffers.Length())
        return false;
    TextureBuffer* tb = m_buffers[bufferIndex];
    if (not tb)
        return false;

    int w = tb->m_info.m_width;
    int h = tb->m_info.m_height;
    if ((w <= 0) or (h <= 0))
        return false;

    if (not CreateTextureResource(w, h, 1))
        return false;
    const uint8_t* pixels = static_cast<const uint8_t*>(tb->DataBuffer());
    if (not UploadTextureData(m_image, m_layoutTracker, pixels, w, h, tb->m_info.m_componentCount))
        return false;
    if (not CreateSRV())
        return false;

    m_isDeployed = true;
    return true;
}


bool Texture::Redeploy(void)
{
    m_isDeployed = false;
    return Deploy(0);
}


bool Texture::Load(String& folder, List<String>& fileNames, const TextureCreationParams& params)
{
    m_filenames = fileNames;
    m_name = fileNames.First();
    TextureBuffer* texBuf = nullptr;
    for (auto& fileName : fileNames) {
        if (fileName.IsEmpty()) {
            if (not texBuf)
                return false;
            ++(texBuf->m_refCount);
            m_buffers.Append(texBuf);
        }
        else {
            String fullPath = folder + "/" + fileName;
            SDL_Surface* image = IMG_Load((const char*)fullPath);
            if (not image) {
                if (params.isRequired)
                    fprintf(stderr, "Texture::Load: failed to load '%s'\n", (const char*)fullPath);
                return false;
            }
            texBuf = new TextureBuffer();
            texBuf->Create(image, params.premultiply, params.flipVertically);
            m_buffers.Append(texBuf);
        }
    }
    return true;
}


bool Texture::CreateFromFile(String folder, List<String>& fileNames, const TextureCreationParams& params)
{
    if (not Create())
        return false;
    if (fileNames.IsEmpty())
        return true;
    if (not Load(folder, fileNames, params))
        return false;
    if (params.cartoonize)
        Cartoonize(params.blur, params.gradients, params.outline);
    m_isDisposable = params.isDisposable;
    return Deploy();
}


bool Texture::CreateFromSurface(SDL_Surface* surface, const TextureCreationParams& params)
{
    if (not Create())
        return false;
    m_buffers.Append(new TextureBuffer(surface, params.premultiply, params.flipVertically));
    m_isDisposable = params.isDisposable;
    return Deploy();
}


void Texture::Cartoonize(uint16_t blurStrength, uint16_t gradients, uint16_t outlinePasses)
{
    for (auto& b : m_buffers)
        b->Cartoonize(blurStrength, gradients, outlinePasses);
}


void Texture::SetWrapping(GfxWrapMode wrapMode) noexcept
{
    m_wrapMode = wrapMode;
    m_sampling.wrapU = wrapMode;
    m_sampling.wrapV = wrapMode;
    m_sampling.wrapW = wrapMode;
}


RenderOffsets Texture::ComputeOffsets(int w, int h, int viewportWidth, int viewportHeight, int renderAreaWidth, int renderAreaHeight)
noexcept
{
    if (renderAreaWidth == 0)
        renderAreaWidth = viewportWidth;
    if (renderAreaHeight == 0)
        renderAreaHeight = viewportHeight;
    float xScale = float(renderAreaWidth) / float(viewportWidth);
    float yScale = float(renderAreaHeight) / float(viewportHeight);
    float wRatio = float(renderAreaWidth) / float(w);
    float hRatio = float(renderAreaHeight) / float(h);
    RenderOffsets offsets = { 0.5f * xScale, 0.5f * yScale };
    if (wRatio > hRatio)
        offsets.x -= (float(renderAreaWidth) - float(w) * hRatio) / float(2 * viewportWidth);
    else if (wRatio < hRatio)
        offsets.y -= (float(renderAreaHeight) - float(h) * wRatio) / float(2 * viewportHeight);
    return offsets;
}

// =================================================================================================

void TiledTexture::SetParams(bool forceUpdate)
{
    if (not (forceUpdate or not m_hasParams))
        return;
    Texture::SetParams(forceUpdate);
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.mipMode = GfxMipMode::Linear;
    m_sampling.maxAnisotropy = 16.0f;
}


void RenderTargetTexture::SetParams(bool forceUpdate)
{
    if (not (forceUpdate or not m_hasParams))
        return;
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Linear;
    m_sampling.magFilter = GfxFilterMode::Linear;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::ClampToEdge;
    m_sampling.wrapV = GfxWrapMode::ClampToEdge;
    m_sampling.wrapW = GfxWrapMode::ClampToEdge;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


void ShadowTexture::SetParams(bool forceUpdate)
{
    if (not (forceUpdate or not m_hasParams))
        return;
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Linear;
    m_sampling.magFilter = GfxFilterMode::Linear;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::ClampToEdge;
    m_sampling.wrapV = GfxWrapMode::ClampToEdge;
    m_sampling.wrapW = GfxWrapMode::ClampToEdge;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Less;
    m_sampling.maxAnisotropy = 1.0f;
}

// =================================================================================================
