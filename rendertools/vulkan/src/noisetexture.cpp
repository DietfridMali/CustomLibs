#define NOMINMAX

// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cstring>

#include "noisetexture.h"
#include "conversions.hpp"
#include "vkupload.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL_image.h"
#pragma warning(pop)

#include "noise.h"

using namespace Noise;

#define NORMALIZE_NOISE 0

#define CLOUD_STRUCTURE 2
#define SPREAD_NOISE    1

// =================================================================================================

bool NoiseTexture3D::Allocate(Vector3i gridDimensions) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_gridDimensions = gridDimensions;
    m_data.Resize(GridSize() * 4);
    // Store dimensions; format tags unused in DX12 (0 = unused)
    texBuf->m_info = TextureBuffer::BufferInfo(gridDimensions.x, gridDimensions.y * gridDimensions.z, 1, 0, 0);
    return true;
}


bool NoiseTexture3D::Create(Vector3i gridDimensions, const NoiseParams& params, String noiseFilename, bool deploy) {
    if (not Texture::Create())
        return false;
    m_type = TextureType::Texture3D;
    if (not Allocate(gridDimensions))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        ComputeNoise();
        SaveToFile(noiseFilename);
    }

    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ 0.0f, 0.0f, 0.0f, 0.0f };
    float* data = m_data.Data();
    StaticArray<uint32_t, 101> d[4];
    for (int i = 0; i < 4; ++i)
        d[i].fill(0);
    for (uint32_t i = m_gridDimensions.x * m_gridDimensions.y * m_gridDimensions.z; i; --i) {
        Vector4f noise;
        noise.x = *data++;
        noise.y = *data++;
        noise.z = *data++;
        noise.w = *data++;
        minVals.Minimize(noise);
        maxVals.Maximize(noise);
    }
    if (deploy)
        Deploy();
    return true;
}


static inline float Saturate(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}


static float Modulate(float shape, float coarseDetail, float mediumDetail, float fineDetail) {
    return Saturate(.6125f * shape + .1625f * coarseDetail + .125f * mediumDetail + .1f * fineDetail);
}


void NoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.Data();

    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ -1e6f, -1e6f, -1e6f, -1e6f };

    CloudNoise generator;

    int i = 0;
    Vector3f p;
    for (int z = 0; z < m_gridDimensions.z; ++z) {
        p.z = (float(z) + 0.5f) / float(m_gridDimensions.z);
        for (int y = 0; y < m_gridDimensions.y; ++y) {
            p.y = (float(y) + 0.5f) / float(m_gridDimensions.y);
            for (int x = 0; x < m_gridDimensions.x; ++x) {
                p.x = (float(x) + 0.5f) / float(m_gridDimensions.x);
                Vector4f noise = generator.Compute(p);
                if (m_params.normalize) {
                    data[i++] = noise.x;
                    data[i++] = noise.y;
                    data[i++] = noise.z;
                    data[i++] = noise.a;
                    minVals.Minimize(noise);
                    maxVals.Maximize(noise);
                }
            }
        }
    }

    data = m_data.Data();
    int dataSize = i / 4;
    for (int i = dataSize; i; --i) {
        for (int j = 0; j < 4; ++j, ++data) {
            *data = (m_params.normalize & (1 << j)) ? Conversions::Normalize(*data, minVals[j], maxVals[j]) : Saturate(*data);
        }
    }
}


void NoiseTexture3D::SetParams(bool /*enforce*/) {
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Linear;
    m_sampling.magFilter = GfxFilterMode::Linear;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


// Helper: create a TYPE_3D image view for the resource owned by `tex`.
// Used by NoiseTexture3D / CloudNoiseTexture / BlueNoiseTexture below — Upload3DTextureData
// builds the VkImage + VmaAllocation and transitions the layout to SHADER_READ_ONLY, but does
// not create the view. The view binds m_image as the t-slot source on Activate.
static bool CreateView3D(Texture& tex, VkFormat fmt) noexcept
{
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE or tex.m_image == VK_NULL_HANDLE)
        return false;
    if (tex.m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, tex.m_imageView, nullptr);
        tex.m_imageView = VK_NULL_HANDLE;
    }
    VkImageViewCreateInfo vci{};
    vci.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                       = tex.m_image;
    vci.viewType                    = VK_IMAGE_VIEW_TYPE_3D;
    vci.format                      = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    return vkCreateImageView(device, &vci, nullptr, &tex.m_imageView) == VK_SUCCESS;
}


bool NoiseTexture3D::Deploy(int) {
    if (m_data.IsEmpty())
        return false;

    // Cloud-shape noise: RGBA32F — 4 channels x float32 per voxel.
    constexpr VkFormat fmt    = VK_FORMAT_R32G32B32A32_SFLOAT;
    constexpr uint32_t stride = 16;  // 4 × float32

    if (m_image != VK_NULL_HANDLE) {
        if (m_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(vkContext.Device(), m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }
        vmaDestroyImage(vkContext.Allocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    if (not Upload3DTextureData(m_gridDimensions.x, m_gridDimensions.y, m_gridDimensions.z,
                                fmt, stride, m_data.Data(),
                                m_image, m_allocation, m_layoutTracker))
        return false;
    if (not CreateView3D(*this, fmt))
        return false;

    m_isValid = true;
    m_isDeployed = true;
    return true;
}


bool NoiseTexture3D::LoadFromFile(const String& filename) {
    if (filename.IsEmpty())
        return false;
    std::ifstream f((const char*)filename, std::ios::binary);
    if (not f)
        return false;

    uint32_t voxelCount = GridSize() * 4;
    uint32_t expectedBytes = voxelCount * sizeof(float);

    f.seekg(0, std::ios::end);
    std::streamoff fileSize = f.tellg();
    f.seekg(0, std::ios::beg);

    if (fileSize != std::streamoff(expectedBytes))
        return false;

    if (m_data.Length() != voxelCount)
        m_data.Resize(voxelCount);

    f.read(reinterpret_cast<char*>(m_data.Data()), expectedBytes);
    return f.good();
}


bool NoiseTexture3D::SaveToFile(const String& filename) const {
    if (filename.IsEmpty())
        return false;
    std::ofstream f((const char*)filename, std::ios::binary | std::ios::trunc);
    if (not f)
        return false;

    uint32_t voxelCount = GridSize() * 4u;
    uint32_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.Data()), bytes);
    return f.good();
}

// =================================================================================================

static float Amp(float v) {
    return 0.5f + 0.5f * cos(v * PI);
}


static float Amp2(float v) {
    return Amp(Amp(v));
}


// Trilinear sample with [0,1) wrap (matches GL_REPEAT / D3D12_TEXTURE_ADDRESS_WRAP).
static float TrilinearSampleWrap(const float* data, int size, float u, float v, float w) {
    u = u - std::floor(u);
    v = v - std::floor(v);
    w = w - std::floor(w);

    float fx = u * float(size) - 0.5f;
    float fy = v * float(size) - 0.5f;
    float fz = w * float(size) - 0.5f;

    int x0 = (int)std::floor(fx);
    int y0 = (int)std::floor(fy);
    int z0 = (int)std::floor(fz);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    float tx = fx - float(x0);
    float ty = fy - float(y0);
    float tz = fz - float(z0);

    auto wrap = [size](int i) { return ((i % size) + size) % size; };
    x0 = wrap(x0);
    x1 = wrap(x1);
    y0 = wrap(y0);
    y1 = wrap(y1);
    z0 = wrap(z0);
    z1 = wrap(z1);

    auto idx = [size](int x, int y, int z) {
        return (z * size + y) * size + x;
    };

    float c000 = data[idx(x0, y0, z0)];
    float c100 = data[idx(x1, y0, z0)];
    float c010 = data[idx(x0, y1, z0)];
    float c110 = data[idx(x1, y1, z0)];
    float c001 = data[idx(x0, y0, z1)];
    float c101 = data[idx(x1, y0, z1)];
    float c011 = data[idx(x0, y1, z1)];
    float c111 = data[idx(x1, y1, z1)];

    float c00 = c000 + tx * (c100 - c000);
    float c10 = c010 + tx * (c110 - c010);
    float c01 = c001 + tx * (c101 - c001);
    float c11 = c011 + tx * (c111 - c011);

    float c0 = c00 + ty * (c10 - c00);
    float c1 = c01 + ty * (c11 - c01);

    return c0 + tz * (c1 - c0);
}


bool CloudNoiseTexture::Allocate(int gridSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_gridSize = gridSize;
    m_data.Resize(size_t(gridSize) * gridSize * gridSize);
    texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize, 1, 0, 0);
    return true;
}


bool CloudNoiseTexture::Create(int gridSize, const NoiseParams& params, String noiseFilename, bool compute) {
    if (not Texture::Create())
        return false;
    m_type = TextureType::Texture3D;
    if (not Allocate(gridSize))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        if (not compute)
            return true;
        std::filesystem::path _p{ noiseFilename.GetStr() };
        Compute(_p.parent_path().string());
        ApplyWarp();
        SaveToFile(noiseFilename);
    }
#if 0
    float* data = m_data.Data();
    float minVal = 1e6f, maxVal = 0.0f;
    for (uint32_t i = m_gridSize * m_gridSize * m_gridSize; i; --i, ++data) {
        float n = *data;
        if (minVal > n) minVal = n;
        if (maxVal < n) maxVal = n;
    }
#endif
    Deploy();
    return true;
}


void CloudNoiseTexture::Compute(String textureFolder) {
    CloudNoise generator;

#if 1
    m_params.normalize = 1 + 2 + 4 + 8;
    NoiseTexture3D rgbaNoise;
    rgbaNoise.Create({ m_gridSize, m_gridSize, m_gridSize }, m_params, textureFolder + "/cloudnoise-rgba.bin", false);

    float* rgbaData = rgbaNoise.GetData().Data();
    float* data = m_data.Data();
    uint32_t dataSize = m_gridSize * m_gridSize * m_gridSize;
#if SPREAD_NOISE
    for (int i = dataSize; i; --i) {
        float perlin = Amp(rgbaData[0]);
        perlin *= perlin;
#if CLOUD_STRUCTURE == 0
        float worley = Amp2(rgbaData[1]) * 0.625f + Amp2(rgbaData[2]) * 0.25f + Amp2(rgbaData[3]) * 0.125f;
#elif CLOUD_STRUCTURE == 1
        float worley = Amp2(rgbaData[1]) * 0.5f   + Amp2(rgbaData[2]) * 0.3f  + Amp2(rgbaData[3]) * 0.2f;
#else
        float worley = Amp2(rgbaData[1]) * 0.65f  + Amp2(rgbaData[2]) * 0.25f + Amp2(rgbaData[3]) * 0.1f;
#endif
        *data++ = generator.Remap(perlin, Amp2(worley) - 1.0f, 1.0f, 0.0f, 1.0f);
        rgbaData += 4;
    }
#else
    for (int i = dataSize; i; --i) {
        float perlin = Amp(rgbaData[0]);
#if CLOUD_STRUCTURE == 0
        float worley = rgbaData[1] * 0.625f + rgbaData[2] * 0.25f + rgbaData[3] * 0.125f;
#elif CLOUD_STRUCTURE == 1
        float worley = rgbaData[1] * 0.5f + rgbaData[2] * 0.3f + rgbaData[3] * 0.2f;
#else
        float worley = rgbaData[1] * 0.65f + rgbaData[2] * 0.25f + rgbaData[3] * 0.1f;
#endif
        *data++ = generator.Remap(perlin, worley - 1.0f, 1.0f, 0.0f, 1.0f);
        rgbaData += 4;
    }
#endif
    rgbaNoise.GetData().Reset();
#else
    Vector3f p;
    for (int z = 0; z < m_gridSize; ++z) {
        p.z = (float(z) + 0.5f) / float(m_gridSize);
        for (int y = 0; y < m_gridSize; ++y) {
            p.y = (float(y) + 0.5f) / float(m_gridSize);
            for (int x = 0; x < m_gridSize; ++x) {
                p.x = (float(x) + 0.5f) / float(m_gridSize);
                Vector4f noise = generator.Compute(p);
                float perlin = Amp(noise.x);
                perlin *= perlin;
                float worley = Amp2(noise.y) * 0.625f + Amp2(noise.z) * 0.125f + Amp2(noise.w) * 0.25f;
                *m_data.Data()++ = generator.Remap(perlin, Amp2(worley) - 1.0f, 1.0f, 0.0f, 1.0f);
            }
        }
    }
#endif
}


// Bake a warp transform into the noise texture: for every voxel p in [0,1)^3,
// store raw[Warped(p)] at p. Drift in the shader becomes a clean translation of a
// static warp-distorted field; the shader's Warped() can be a no-op (applyWarp=0).
//
// Variant is selected via m_params.warping:
//   Infinite — linear cross-axis warp (0.37*p.z, 0.41*p.x). Not periodic in p, so
//              final[] is not periodic either; usable only when the entire visible
//              world fits in one texture tile (small cloudScale, no GL_REPEAT wrap
//              during sampling). Tile-seam visible otherwise.
//   Periodic — sin(2*pi*p)/(2*pi) replaces the linear cross-axis term. final[] is
//              periodic; tile-seam disappears, slight tile-repetition possible.
//   None     — no pre-warp; raw noise stored as-is. Shader can still apply runtime
//              warp via applyWarp uniform.
void CloudNoiseTexture::ApplyWarp(void) {
    switch (m_params.warping) {
        case NoiseWarp::Infinite:
            ApplyInfiniteWarp();
            break;
        case NoiseWarp::Periodic:
            ApplyPeriodicWarp();
            break;
        case NoiseWarp::None:
        default:
            break;
    }
}


// Linear cross-axis warp (matches the shader's original Warped() before pre-warp).
// Not periodic in p — see warning in ApplyWarp().
void CloudNoiseTexture::ApplyInfiniteWarp(void) {
    static const float warpStrength = 0.25f;

    uint32_t totalVoxels = uint32_t(m_gridSize) * uint32_t(m_gridSize) * uint32_t(m_gridSize);

    AutoArray<float> raw;
    raw.Resize(totalVoxels);
    std::memcpy(raw.Data(), m_data.Data(), size_t(totalVoxels) * sizeof(float));

    const float* src = raw.Data();
    float* dst = m_data.Data();
    const float invSize = 1.0f / float(m_gridSize);

    for (int z = 0; z < m_gridSize; ++z) {
        float pz = (float(z) + 0.5f) * invSize;
        for (int y = 0; y < m_gridSize; ++y) {
            float py = (float(y) + 0.5f) * invSize;
            for (int x = 0; x < m_gridSize; ++x) {
                float px = (float(x) + 0.5f) * invSize;

                float n = TrilinearSampleWrap(src, m_gridSize, px * 0.3f, py * 0.3f, pz * 0.3f);
                float localWarp = warpStrength * (0.5f + n);

                float wx = px + 0.37f * pz;
                float wz = pz + 0.41f * px;
                float warpedX = px + localWarp * (wx - px);
                float warpedY = py;
                float warpedZ = pz + localWarp * (wz - pz);

                *dst++ = TrilinearSampleWrap(src, m_gridSize, warpedX, warpedY, warpedZ);
            }
        }
    }
}


// Periodic cross-axis warp — sin(2*pi*p)/(2*pi) is 1-periodic in p so final[] stays
// seamless across tile boundaries. Amplitude factor 1/(2*pi) keeps max displacement
// comparable to the linear form (peak ~0.059 vs 0.37 at p.z=1).
void CloudNoiseTexture::ApplyPeriodicWarp(void) {
    static const float warpStrength = 0.25f;
    static const float kTwoPi = 6.28318530717958647692f;
    static const float kInvTwoPi = 1.0f / kTwoPi;

    uint32_t totalVoxels = uint32_t(m_gridSize) * uint32_t(m_gridSize) * uint32_t(m_gridSize);

    AutoArray<float> raw;
    raw.Resize(totalVoxels);
    std::memcpy(raw.Data(), m_data.Data(), size_t(totalVoxels) * sizeof(float));

    const float* src = raw.Data();
    float* dst = m_data.Data();
    const float invSize = 1.0f / float(m_gridSize);

    for (int z = 0; z < m_gridSize; ++z) {
        float pz = (float(z) + 0.5f) * invSize;
        for (int y = 0; y < m_gridSize; ++y) {
            float py = (float(y) + 0.5f) * invSize;
            for (int x = 0; x < m_gridSize; ++x) {
                float px = (float(x) + 0.5f) * invSize;

                float n = TrilinearSampleWrap(src, m_gridSize, px * 0.3f, py * 0.3f, pz * 0.3f);
                float localWarp = warpStrength * (0.5f + n);

                float wx = px + 0.37f * std::sin(kTwoPi * pz) * kInvTwoPi;
                float wz = pz + 0.41f * std::sin(kTwoPi * px) * kInvTwoPi;
                float warpedX = px + localWarp * (wx - px);
                float warpedY = py;
                float warpedZ = pz + localWarp * (wz - pz);

                *dst++ = TrilinearSampleWrap(src, m_gridSize, warpedX, warpedY, warpedZ);
            }
        }
    }
}


void CloudNoiseTexture::SetParams(bool /*enforce*/) {
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Linear;
    m_sampling.magFilter = GfxFilterMode::Linear;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


bool CloudNoiseTexture::Deploy(int) {
    if (m_data.IsEmpty())
        return false;

    // Single-channel float32 → R32_SFLOAT
    constexpr VkFormat fmt    = VK_FORMAT_R32_SFLOAT;
    constexpr uint32_t stride = 4;

    if (m_image != VK_NULL_HANDLE) {
        if (m_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(vkContext.Device(), m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }
        vmaDestroyImage(vkContext.Allocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    if (not Upload3DTextureData(m_gridSize, m_gridSize, m_gridSize,
                                fmt, stride, m_data.Data(),
                                m_image, m_allocation, m_layoutTracker))
        return false;
    if (not CreateView3D(*this, fmt))
        return false;

    m_isValid = true;
    m_isDeployed = true;
    return true;
}


bool CloudNoiseTexture::LoadFromFile(const String& filename) {
    return m_data.LoadFromFile(filename, size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize));
}


bool CloudNoiseTexture::SaveToFile(const String& filename) const {
    return m_data.SaveToFile(filename);
}


static bool IsPowerOfTwo(int n) {
    return (n > 0) and ((n & (n - 1)) == 0);
}


void CloudNoiseTexture::DownSample(float* src, int srcEdgeLen, float* dest, int destEdgeLen) {
    if ((src == nullptr) or (dest == nullptr))
        return;
    if (not IsPowerOfTwo(srcEdgeLen))
        return;
    if (not IsPowerOfTwo(destEdgeLen))
        return;
    if (destEdgeLen >= srcEdgeLen)
        return;

    const int ratio = srcEdgeLen / destEdgeLen;
    const size_t srcRow = size_t(srcEdgeLen);
    const size_t srcSlice = srcRow * srcRow;
    const size_t destRow = size_t(destEdgeLen);
    const size_t destSlice = destRow * destRow;

    for (int zd = 0; zd < destEdgeLen; ++zd) {
        const int sz0 = zd * ratio;
        for (int yd = 0; yd < destEdgeLen; ++yd) {
            const int sy0 = yd * ratio;
            for (int xd = 0; xd < destEdgeLen; ++xd) {
                const int sx0 = xd * ratio;
                float maxVal = src[size_t(sz0) * srcSlice + size_t(sy0) * srcRow + size_t(sx0)];
                for (int iz = 0; iz < ratio; ++iz) {
                    const size_t zOff = size_t(sz0 + iz) * srcSlice;
                    for (int iy = 0; iy < ratio; ++iy) {
                        const size_t yOff = size_t(sy0 + iy) * srcRow;
                        for (int ix = 0; ix < ratio; ++ix) {
                            const float v = src[zOff + yOff + size_t(sx0 + ix)];
                            if (maxVal < v)
                                maxVal = v;
                        }
                    }
                }
                dest[size_t(zd) * destSlice + size_t(yd) * destRow + size_t(xd)] = maxVal;
            }
        }
    }
}


void CloudNoiseTexture::ToMaxMip(CloudNoiseTexture* mipTex) {
    if (mipTex == nullptr)
        return;
    DownSample(m_data.Data(), m_gridSize, mipTex->m_data.Data(), mipTex->m_gridSize);
}


CloudNoiseTexture* CloudNoiseTexture::CreateMaxMip(int mipSize, String noiseFilename) {
    if (not IsPowerOfTwo(m_gridSize))
        return nullptr;
    if (not IsPowerOfTwo(mipSize))
        return nullptr;
    if (mipSize >= m_gridSize)
        return nullptr;

    CloudNoiseTexture* mipTex = new NoiseMaxMipTexture();
    if (mipTex == nullptr)
        return nullptr;

    if (not mipTex->Create(mipSize, m_params, noiseFilename, false)) {
        delete mipTex;
        return nullptr;
    }

    if (mipTex->IsDeployed())
        return mipTex;

    ToMaxMip(mipTex);
    if (not mipTex->Deploy()) {
        delete mipTex;
        return nullptr;
    }
    mipTex->SaveToFile(noiseFilename);
    return mipTex;
}

// =================================================================================================

void NoiseMaxMipTexture::SetParams(bool /*enforce*/) {
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Nearest;
    m_sampling.magFilter = GfxFilterMode::Nearest;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}

// =================================================================================================

bool BlueNoiseTexture::Allocate(void) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_data.Resize(BufferSize());
    texBuf->m_info = TextureBuffer::BufferInfo(m_gridSize.x, m_gridSize.y * 64, 1, 0, 0);
    Validate();
    return true;
}


bool BlueNoiseTexture::Create(String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = TextureType::Texture3D;
    if (not Allocate())
        return false;
    if (not LoadFromFile(noiseFilename)) {
        std::filesystem::path _p{ noiseFilename.GetStr() };
        Compute(_p.parent_path().string());
        SaveToFile(noiseFilename);
    }
    Deploy();
    return true;
}


void BlueNoiseTexture::Compute(String textureFolder) {
    uint32_t layerSize = m_gridSize.x * m_gridSize.y;
    for (int i = 0; i < 64; ++i) {
        String filename = textureFolder + "/bluenoise/stbn_scalar_2Dx1Dx1D_128x128x64x1_" + String(i) + ".png";
        SDL_Surface* image = IMG_Load(filename.Data());
        if (image) {
            std::memcpy(m_data.Data(layerSize * i), image->pixels, layerSize);
            SDL_FreeSurface(image);
        }
    }
}


void BlueNoiseTexture::SetParams(bool /*enforce*/) {
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Nearest;
    m_sampling.magFilter = GfxFilterMode::Nearest;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


bool BlueNoiseTexture::Deploy(int) {
    if (m_data.IsEmpty())
        return false;

    constexpr VkFormat fmt    = VK_FORMAT_R8_UNORM;
    constexpr uint32_t stride = 1;

    if (m_image != VK_NULL_HANDLE) {
        if (m_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(vkContext.Device(), m_imageView, nullptr);
            m_imageView = VK_NULL_HANDLE;
        }
        vmaDestroyImage(vkContext.Allocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    if (not Upload3DTextureData(128, 128, 64, fmt, stride, m_data.Data(),
                                m_image, m_allocation, m_layoutTracker))
        return false;
    if (not CreateView3D(*this, fmt))
        return false;

    m_isValid = true;
    m_isDeployed = true;
    return true;
}


bool BlueNoiseTexture::LoadFromFile(const String& filename) {
    return m_data.LoadFromFile(filename, BufferSize());
}


bool BlueNoiseTexture::SaveToFile(const String& filename) {
    return m_data.SaveToFile(filename);
}

// =================================================================================================
