#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "rendertypes.h"

// =================================================================================================
// API-neutral sampler configuration carried by every Texture.
//
// A Texture stores its desired sampling parameters in TextureSampling. The active
// graphics backend (DX12 / Vulkan) translates this struct into its own sampler
// representation on first bind and caches the result so that textures sharing the
// same configuration share the same backend sampler object.
//
// OGL and Vulkan keep min/mag/mip filters as separate values; DX12 packs them into
// a single D3D12_FILTER value at translation time. The struct exposes the three
// fields separately (variant a) so that translation is a pure mapping in either
// direction.

enum class GfxFilterMode : uint8_t {
    Nearest = 0,
    Linear  = 1
};

enum class GfxMipMode : uint8_t {
    None    = 0,    // no mipmaps; sampling stays on base level
    Nearest = 1,
    Linear  = 2
};

// =================================================================================================

#pragma pack(push, 1)
struct TextureSampling
{
    GfxFilterMode               minFilter      { GfxFilterMode::Linear };
    GfxFilterMode               magFilter      { GfxFilterMode::Linear };
    GfxMipMode                  mipMode        { GfxMipMode::None };
    GfxWrapMode                 wrapU          { GfxWrapMode::Repeat };
    GfxWrapMode                 wrapV          { GfxWrapMode::Repeat };
    GfxWrapMode                 wrapW          { GfxWrapMode::Repeat };
    GfxOperations::CompareFunc  compareFunc    { GfxOperations::CompareFunc::Always };
    float                       maxAnisotropy  { 1.0f };
    float                       mipLodBias     { 0.0f };
    float                       minLOD         { 0.0f };
    float                       maxLOD         { 1.0e30f };
    float                       borderColor[4] { 0.0f, 0.0f, 0.0f, 0.0f };


    bool operator==(const TextureSampling& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) == 0;
    }


    bool operator!=(const TextureSampling& o) const noexcept {
        return not (*this == o);
    }


    bool operator<(const TextureSampling& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) < 0;
    }
};
#pragma pack(pop)

// =================================================================================================
