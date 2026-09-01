#pragma once

#include "rendertypes.h"
#include "vkframework.h"

// =================================================================================================
// Vulkan mapping for the platform-neutral GfxPixelFormat enum (defined in rendertypes.h).
// Returns the VkFormat used both for VkImage creation and the VkImageView storage view.

inline constexpr VkFormat ToVkFormat(GfxPixelFormat f) noexcept {
    switch (f) {
        case GfxPixelFormat::R8_UNorm:       return VK_FORMAT_R8_UNORM;
        case GfxPixelFormat::RG8_UNorm:      return VK_FORMAT_R8G8_UNORM;
        case GfxPixelFormat::RGBA8_UNorm:    return VK_FORMAT_R8G8B8A8_UNORM;
        case GfxPixelFormat::R16_SFloat:     return VK_FORMAT_R16_SFLOAT;
        case GfxPixelFormat::R32_SFloat:     return VK_FORMAT_R32_SFLOAT;
        case GfxPixelFormat::RGBA16_SFloat:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GfxPixelFormat::RGBA32_SFloat:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        // B10G11R11 and not R11G11B10: Vulkan names the packed formats least significant channel
        // first, so this is the SAME memory layout as DXGI_FORMAT_R11G11B10_FLOAT and GL_R11F_G11F_B10F.
        case GfxPixelFormat::RG11B10_SFloat: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case GfxPixelFormat::BC1_UNorm:      return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case GfxPixelFormat::BC7_UNorm:      return VK_FORMAT_BC7_UNORM_BLOCK;
        case GfxPixelFormat::BC4_UNorm:      return VK_FORMAT_BC4_UNORM_BLOCK;
        case GfxPixelFormat::BC5_UNorm:      return VK_FORMAT_BC5_UNORM_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}



// The same mapping under a name that is spelled identically in all three backends, so that code
// outside the backend directories (TextureAtlas and friends) can fill RTCreationParams::colorFormat
// without knowing whether that field is a GLenum, a DXGI_FORMAT or a VkFormat.
inline constexpr VkFormat ToNativeColorFormat(GfxPixelFormat f) noexcept {
    return ToVkFormat(f);
}

// =================================================================================================
