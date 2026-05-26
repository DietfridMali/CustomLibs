#pragma once

#include "rendertypes.h"
#include "array.hpp"

#include <cstdint>

// =================================================================================================
// CPU-side 3D mipmap generation. Builds a complete mip chain from a source 3D texture via 2×2×2
// box filter, so the VK and DX12 upload paths can feed all levels to the GPU at create time.
// (OpenGL's Upload3DTexture uses glGenerateMipmap instead — driver-side equivalent.) Behavior is
// functionally identical across all three backends: every 3D texture lands on the GPU with a
// full mip pyramid, addressable via SampleLod(tex, s, uvw, lod).
//
// Supported pixel formats: R8_UNorm, R32_SFloat, RGBA32_SFloat. Other formats produce a
// zero-filled chain (the half-precision variants are OGL-only and never reach this path).

struct MipLevel3D {
    int                  width  { 0 };
    int                  height { 0 };
    int                  depth  { 0 };
    AutoArray<uint8_t>   data;
};

// Floor(log2(max(w, h, d))) + 1. The standard mip-count formula.
int CalcMipLevels(int width, int height, int depth) noexcept;

// On return outChain has CalcMipLevels(w,h,d) entries: level 0 is a copy of src, levels 1..N-1
// are successively halved (each dimension max(1, prev/2)) with channel-wise averaging.
void BuildMipChain3D(const void* src, int width, int height, int depth,
                     GfxPixelFormat fmt,
                     AutoArray<MipLevel3D>& outChain) noexcept;

// =================================================================================================
