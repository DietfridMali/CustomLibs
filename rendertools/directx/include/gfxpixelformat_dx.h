#pragma once

#include "rendertypes.h"
#include "dx12framework.h"

// =================================================================================================
// DirectX 12 mapping for the platform-neutral GfxPixelFormat enum (defined in rendertypes.h).
// Returns the DXGI_FORMAT used both for resource creation and SRV typing.

inline constexpr DXGI_FORMAT ToDXGIFormat(GfxPixelFormat f) noexcept {
    switch (f) {
        case GfxPixelFormat::R8_UNorm:       return DXGI_FORMAT_R8_UNORM;
        case GfxPixelFormat::RG8_UNorm:      return DXGI_FORMAT_R8G8_UNORM;
        case GfxPixelFormat::RGBA8_UNorm:    return DXGI_FORMAT_R8G8B8A8_UNORM;
        case GfxPixelFormat::R16_SFloat:     return DXGI_FORMAT_R16_FLOAT;
        case GfxPixelFormat::R32_SFloat:     return DXGI_FORMAT_R32_FLOAT;
        case GfxPixelFormat::RGBA16_SFloat:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case GfxPixelFormat::RGBA32_SFloat:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

// =================================================================================================
