#define NOMINMAX

#include "texture_mips.h"

#include <algorithm>
#include <cstring>

// =================================================================================================

int CalcMipLevels(int w, int h, int d) noexcept
{
    int levels = 1;
    while ((w > 1) or (h > 1) or (d > 1)) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        d = std::max(1, d / 2);
        ++levels;
    }
    return levels;
}

// -------------------------------------------------------------------------------------------------
// Per-format 2×2×2 box-filter downsamples. Out-of-bounds source texels (for dimensions that are
// not powers of two) are clamped to the source extent — the standard convention used by
// glGenerateMipmap implementations on the OpenGL side.

static void Downsample3D_R32F(const float* src, int sw, int sh, int sd,
                              float* dst, int dw, int dh, int dd) noexcept
{
    for (int zd = 0; zd < dd; ++zd) {
        for (int yd = 0; yd < dh; ++yd) {
            for (int xd = 0; xd < dw; ++xd) {
                float sum = 0.0f;
                int count = 0;
                for (int dz = 0; dz < 2; ++dz) {
                    int zs = std::min(zd * 2 + dz, sd - 1);
                    for (int dy = 0; dy < 2; ++dy) {
                        int ys = std::min(yd * 2 + dy, sh - 1);
                        for (int dx = 0; dx < 2; ++dx) {
                            int xs = std::min(xd * 2 + dx, sw - 1);
                            sum += src[(size_t(zs) * size_t(sh) + size_t(ys)) * size_t(sw) + size_t(xs)];
                            ++count;
                        }
                    }
                }
                dst[(size_t(zd) * size_t(dh) + size_t(yd)) * size_t(dw) + size_t(xd)] = sum / float(count);
            }
        }
    }
}


static void Downsample3D_RGBA32F(const float* src, int sw, int sh, int sd,
                                 float* dst, int dw, int dh, int dd) noexcept
{
    for (int zd = 0; zd < dd; ++zd) {
        for (int yd = 0; yd < dh; ++yd) {
            for (int xd = 0; xd < dw; ++xd) {
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                int count = 0;
                for (int dz = 0; dz < 2; ++dz) {
                    int zs = std::min(zd * 2 + dz, sd - 1);
                    for (int dy = 0; dy < 2; ++dy) {
                        int ys = std::min(yd * 2 + dy, sh - 1);
                        for (int dx = 0; dx < 2; ++dx) {
                            int xs = std::min(xd * 2 + dx, sw - 1);
                            size_t idx = ((size_t(zs) * size_t(sh) + size_t(ys)) * size_t(sw) + size_t(xs)) * 4;
                            r += src[idx + 0];
                            g += src[idx + 1];
                            b += src[idx + 2];
                            a += src[idx + 3];
                            ++count;
                        }
                    }
                }
                float inv = 1.0f / float(count);
                size_t out = ((size_t(zd) * size_t(dh) + size_t(yd)) * size_t(dw) + size_t(xd)) * 4;
                dst[out + 0] = r * inv;
                dst[out + 1] = g * inv;
                dst[out + 2] = b * inv;
                dst[out + 3] = a * inv;
            }
        }
    }
}


static void Downsample3D_R8(const uint8_t* src, int sw, int sh, int sd,
                            uint8_t* dst, int dw, int dh, int dd) noexcept
{
    for (int zd = 0; zd < dd; ++zd) {
        for (int yd = 0; yd < dh; ++yd) {
            for (int xd = 0; xd < dw; ++xd) {
                int sum = 0;
                int count = 0;
                for (int dz = 0; dz < 2; ++dz) {
                    int zs = std::min(zd * 2 + dz, sd - 1);
                    for (int dy = 0; dy < 2; ++dy) {
                        int ys = std::min(yd * 2 + dy, sh - 1);
                        for (int dx = 0; dx < 2; ++dx) {
                            int xs = std::min(xd * 2 + dx, sw - 1);
                            sum += int(src[(size_t(zs) * size_t(sh) + size_t(ys)) * size_t(sw) + size_t(xs)]);
                            ++count;
                        }
                    }
                }
                dst[(size_t(zd) * size_t(dh) + size_t(yd)) * size_t(dw) + size_t(xd)]
                    = uint8_t((sum + count / 2) / count);
            }
        }
    }
}

// =================================================================================================

void BuildMipChain3D(const void* src, int w, int h, int d, GfxPixelFormat fmt,
                     AutoArray<MipLevel3D>& outChain) noexcept
{
    const uint32_t stride = GfxPixelStride(fmt);
    const int levels = CalcMipLevels(w, h, d);

    outChain.Resize(uint32_t(levels));

    // Level 0 — direct copy of source.
    MipLevel3D& l0 = outChain[0];
    l0.width  = w;
    l0.height = h;
    l0.depth  = d;
    size_t bytes0 = size_t(w) * size_t(h) * size_t(d) * size_t(stride);
    l0.data.Resize(uint32_t(bytes0));
    std::memcpy(l0.data.DataPtr(), src, bytes0);

    // Levels 1..N-1 — downsample from previous level via 2³ box filter.
    for (int lv = 1; lv < levels; ++lv) {
        const MipLevel3D& prev = outChain[lv - 1];
        MipLevel3D& cur = outChain[lv];
        cur.width  = std::max(1, prev.width  / 2);
        cur.height = std::max(1, prev.height / 2);
        cur.depth  = std::max(1, prev.depth  / 2);
        size_t bytes = size_t(cur.width) * size_t(cur.height) * size_t(cur.depth) * size_t(stride);
        cur.data.Resize(uint32_t(bytes));

        switch (fmt) {
        case GfxPixelFormat::R32_SFloat:
            Downsample3D_R32F(reinterpret_cast<const float*>(prev.data.DataPtr()),
                              prev.width, prev.height, prev.depth,
                              reinterpret_cast<float*>(cur.data.DataPtr()),
                              cur.width, cur.height, cur.depth);
            break;

        case GfxPixelFormat::RGBA32_SFloat:
            Downsample3D_RGBA32F(reinterpret_cast<const float*>(prev.data.DataPtr()),
                                 prev.width, prev.height, prev.depth,
                                 reinterpret_cast<float*>(cur.data.DataPtr()),
                                 cur.width, cur.height, cur.depth);
            break;

        case GfxPixelFormat::R8_UNorm:
            Downsample3D_R8(prev.data.DataPtr(), prev.width, prev.height, prev.depth,
                            cur.data.DataPtr(), cur.width, cur.height, cur.depth);
            break;

        default:
            // Half-precision float formats are OGL-only and never reach this path. Zero-fill so
            // callers won't read uninitialised memory in case the path is exercised by mistake.
            std::memset(cur.data.DataPtr(), 0, bytes);
            break;
        }
    }
}

// =================================================================================================
