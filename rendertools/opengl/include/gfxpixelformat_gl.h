#pragma once

#include "rendertypes.h"
#include "glew.h"

// =================================================================================================
// OpenGL mapping for the platform-neutral GfxPixelFormat enum (defined in rendertypes.h).
//
// Three GLenums per format: internalFormat = how the driver stores the texture, externalFormat +
// type = how the CPU-side buffer is interpreted by glTexImageNd. The internal storage matches the
// historical per-format choice in the noise textures (e.g. R16_SFloat -> GL_R16F, deliberately
// half-precision storage even though clients hand in float32 data); the external upload format is
// the matching float / unsigned-byte variant for the source buffer.

struct GLFormat {
    GLenum internalFormat;
    GLenum externalFormat;
    GLenum type;
};

inline constexpr GLFormat ToGLFormat(GfxPixelFormat f) noexcept {
    switch (f) {
        case GfxPixelFormat::R8_UNorm:       return { GL_R8,      GL_RED,  GL_UNSIGNED_BYTE };
        case GfxPixelFormat::RG8_UNorm:      return { GL_RG8,     GL_RG,   GL_UNSIGNED_BYTE };
        case GfxPixelFormat::RGBA8_UNorm:    return { GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE };
        case GfxPixelFormat::R16_SFloat:     return { GL_R16F,    GL_RED,  GL_FLOAT };
        case GfxPixelFormat::R32_SFloat:     return { GL_R32F,    GL_RED,  GL_FLOAT };
        case GfxPixelFormat::RGBA16_SFloat:  return { GL_RGBA16F, GL_RGBA, GL_FLOAT };
        case GfxPixelFormat::RGBA32_SFloat:  return { GL_RGBA32F, GL_RGBA, GL_FLOAT };
    }
    return { GL_R8, GL_RED, GL_UNSIGNED_BYTE };
}

// =================================================================================================
