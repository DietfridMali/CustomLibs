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
        case GfxPixelFormat::BC1_UNorm:      return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case GfxPixelFormat::BC7_UNorm:      return VK_FORMAT_BC7_UNORM_BLOCK;
        case GfxPixelFormat::BC4_UNorm:      return VK_FORMAT_BC4_UNORM_BLOCK;
        case GfxPixelFormat::BC5_UNorm:      return VK_FORMAT_BC5_UNORM_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

// =================================================================================================
