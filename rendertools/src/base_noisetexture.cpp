#define NOMINMAX

// --- API-neutral noise-texture implementation ---------------------------------------------------
// Backend-specific Deploy + SetParams overrides live in <api>/src/noisetexture.cpp; this unit
// holds everything else (allocation bookkeeping, noise generation, FBM/Worley mixing, warp
// prebake, mip downsampling, file I/O).

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cstring>

#include "base_noisetexture.h"
#include "noisetexture.h"          // resolves per-backend: needed for the concrete NoiseTexture3D
                                   // instance that BaseCloudNoiseTexture::Compute uses to source the
                                   // intermediate RGBA cloud noise.
#include "conversions.hpp"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL_image.h"
#pragma warning(pop)

#include "noise.h"

using namespace Noise;

#define NORMALIZE_NOISE 0
#define CLOUD_STRUCTURE 0
#define SPREAD_NOISE    0

// =================================================================================================
// Local helpers shared between BaseNoiseTexture3D and BaseCloudNoiseTexture.

static inline float Saturate(float v) noexcept {
    return std::clamp(v, 0.0f, 1.0f);
}


static float Modulate(float shape, float coarseDetail, float mediumDetail, float fineDetail) noexcept {
    return Saturate(.6125f * shape + .1625f * coarseDetail + .125f * mediumDetail + .1f * fineDetail);
}


static bool IsPowerOfTwo(int n) noexcept {
    return (n > 0) and ((n & (n - 1)) == 0);
}


// Trilinear sample with [0,1) wrap (matches GL_REPEAT / D3D12_TEXTURE_ADDRESS_WRAP).
static float TrilinearSampleWrap(const float* data, int size, float u, float v, float w) noexcept {
    u = u - std::floor(u);
    v = v - std::floor(v);
    w = w - std::floor(w);

    float fx = u * float(size) - 0.5f;
    float fy = v * float(size) - 0.5f;
    float fz = w * float(size) - 0.5f;

    int x0 = int(std::floor(fx));
    int y0 = int(std::floor(fy));
    int z0 = int(std::floor(fz));
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


// =================================================================================================
// Channel transfer for cloud-noise mixing. SPREAD_NOISE = 1 widens contrast via a half-cosine
// S-curve; SPREAD_NOISE = 0 is identity (raw 0..1 noise).

#if SPREAD_NOISE

static float Amp(float v) {
    return 0.5f - 0.5f * cos(v * PI);
}

static float InvAmp(float v) {
    return 0.5f + 0.5f * cos(v * PI);
}

#   if SPREAD_NOISE == 2

static float Amp2(float v) {
    return Amp(Amp(v));
}

#   else
#       define Amp2(v) Amp(v)
#   endif

#else

#define Amp(v) (v)
#define InvAmp(v) (v)
#define Amp2(v) (v)

#endif

// =================================================================================================
// BaseNoiseTexture3D — RGBA float cloud-noise source texture.

bool BaseNoiseTexture3D::Allocate(Vector3i gridDimensions) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_gridDimensions = gridDimensions;
    m_data.Resize(GridSize() * 4);
    texBuf->m_info = TextureBuffer::BufferInfo(gridDimensions.x, gridDimensions.y * gridDimensions.z, 1, 0, 0);
    return true;
}


bool BaseNoiseTexture3D::Create(Vector3i gridDimensions, const NoiseParams& params, String noiseFilename, bool deploy)
{
    if (not Texture::Create())
        return false;
    SetType(TextureType::Texture3D);
    if (not Allocate(gridDimensions))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        ComputeNoise();
        SaveToFile(noiseFilename);
    }
    return deploy ? Deploy() : true;
}


void BaseNoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.DataPtr();

    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ -1e6f, -1e6f, -1e6f, -1e6f };

    CloudNoise generator;
    generator.SetFbmParams(m_params.perlinParams, m_params.worleyParams);

    int i = 0;
    Vector3f p;
    for (int z = 0; z < m_gridDimensions.z; ++z) {
        p.z = (float(z) + 0.5f) / float(m_gridDimensions.z);
        for (int y = 0; y < m_gridDimensions.y; ++y) {
            p.y = (float(y) + 0.5f) / float(m_gridDimensions.y);
            for (int x = 0; x < m_gridDimensions.x; ++x) {
                p.x = (float(x) + 0.5f) / float(m_gridDimensions.x);
                Vector4f noise = generator.Compute(p);
                data[i++] = noise.x;
                data[i++] = noise.y;
                data[i++] = noise.z;
                data[i++] = noise.a;
                if (m_params.normalize) {
                    minVals.Minimize(noise);
                    maxVals.Maximize(noise);
                }
            }
        }
    }

    data = m_data.DataPtr();
    int dataSize = i / 4;
    for (int j = dataSize; j; --j) {
        for (int k = 0; k < 4; ++k, ++data) {
            *data = (m_params.normalize & (1 << k))
                  ? Conversions::Normalize(*data, minVals[k], maxVals[k])
                  : Saturate(*data);
        }
    }
}


bool BaseNoiseTexture3D::LoadFromFile(const String& filename) {
    if (filename.IsEmpty())
        return false;
    std::ifstream f((const char*) filename, std::ios::binary);
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

    f.read(reinterpret_cast<char*>(m_data.DataPtr()), expectedBytes);
    return f.good();
}


bool BaseNoiseTexture3D::SaveToFile(const String& filename) const {
    if (filename.IsEmpty())
        return false;
    std::ofstream f((const char*) filename, std::ios::binary | std::ios::trunc);
    if (not f)
        return false;

    uint32_t voxelCount = GridSize() * 4u;
    uint32_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.DataPtr()), bytes);
    return f.good();
}


// =================================================================================================
// BaseCloudNoiseTexture — final R-channel cloud-shape texture, computed by Remap-mixing the RGBA
// channels of a temporary NoiseTexture3D (the per-API concrete subclass).

bool BaseCloudNoiseTexture::Allocate(int gridSize) {
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


bool BaseCloudNoiseTexture::Create(int gridSize, const NoiseParams& params, String noiseFilename, bool compute)
{
    if (not Texture::Create())
        return false;
    SetType(TextureType::Texture3D);
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
    return Deploy();
}


void BaseCloudNoiseTexture::Compute(String textureFolder) {
    CloudNoise generator;

    NoiseTexture3D rgbaNoise;
    rgbaNoise.Create({ m_gridSize, m_gridSize, m_gridSize }, m_params, textureFolder + "/cloudnoise-rgba.bin", false);

    float* rgbaData = rgbaNoise.GetData().DataPtr();
    float* data = m_data.DataPtr();
    uint32_t dataSize = m_gridSize * m_gridSize * m_gridSize;

    float dMin = 1.0f, dMax = 0.0f;
    for (int i = dataSize; i; --i) {
        float perlin = InvAmp(rgbaData[0]);
#if SPREAD_NOISE == 1
        perlin = float(pow(perlin, 1.5f));
#elif SPREAD_NOISE == 2
        perlin *= perlin;
#endif
#if CLOUD_STRUCTURE == 0 // standard distribution
        float worley = Amp2(rgbaData[1]) * 0.625f + Amp2(rgbaData[2]) * 0.25f + Amp2(rgbaData[3]) * 0.125f;
#elif CLOUD_STRUCTURE == 1 // less coarser structures, more detail
        float worley = Amp2(rgbaData[1]) * 0.5f + Amp2(rgbaData[2]) * 0.3f + Amp2(rgbaData[3]) * 0.2f;
#else // bigger coarsers structures, less detail
        float worley = Amp2(rgbaData[1]) * 0.65f + Amp2(rgbaData[2]) * 0.25f + Amp2(rgbaData[3]) * 0.1f;
#endif
#if 1
        float d = generator.Remap(perlin, Amp2(worley) - 1.0f, 1.0f, 0.0f, 1.0f);
#else
        float d = perlin * (1.0f - 0.5f * Amp2(worley));
#endif
        if (d < dMin)
            dMin = d;
        if (d > dMax)
            dMax = d;
        *data++ = d;
        rgbaData += 4;
    }
#if 1 //def _DEBUG
    if (dMax - dMin < 0.999f) {
        data = m_data.DataPtr();
        for (int i = dataSize; i; --i, ++data)
            *data = generator.Remap(*data, dMin, dMax, 0.0f, 1.0f);
    }
#endif
    rgbaNoise.GetData().Reset();
}


// Bake a warp transform into the noise texture: for every voxel p in [0,1)^3, store raw[Warped(p)]
// at p. Drift in the shader becomes a clean translation of a static warp-distorted field; the
// shader's Warped() can be a no-op (applyWarp=0). Variant selected via m_params.warping.
void BaseCloudNoiseTexture::ApplyWarp(void) {
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


// Linear cross-axis warp — not periodic in p; only usable when the entire visible world fits in
// one texture tile (small cloudScale, no wrap sampling).
void BaseCloudNoiseTexture::ApplyInfiniteWarp(void) {
    static const float WarpStrength = 0.25f;

    uint32_t totalVoxels = uint32_t(m_gridSize) * uint32_t(m_gridSize) * uint32_t(m_gridSize);

    AutoArray<float> raw;
    raw.Resize(totalVoxels);
    std::memcpy(raw.DataPtr(), m_data.DataPtr(), size_t(totalVoxels) * sizeof(float));

    const float* src = raw.DataPtr();
    float* dst = m_data.DataPtr();
    const float invSize = 1.0f / float(m_gridSize);

    for (int z = 0; z < m_gridSize; ++z) {
        float pz = (float(z) + 0.5f) * invSize;
        for (int y = 0; y < m_gridSize; ++y) {
            float py = (float(y) + 0.5f) * invSize;
            for (int x = 0; x < m_gridSize; ++x) {
                float px = (float(x) + 0.5f) * invSize;

                float n = TrilinearSampleWrap(src, m_gridSize, px * 0.3f, py * 0.3f, pz * 0.3f);
                float localWarp = WarpStrength * (0.5f + n);

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


// Periodic cross-axis warp — sin(2*pi*p)/(2*pi) keeps final[] seamless across tile boundaries.
void BaseCloudNoiseTexture::ApplyPeriodicWarp(void) {
    static const float WarpStrength = 0.25f;
    static const float kTwoPi = 6.28318530717958647692f;
    static const float kInvTwoPi = 1.0f / kTwoPi;

    uint32_t totalVoxels = uint32_t(m_gridSize) * uint32_t(m_gridSize) * uint32_t(m_gridSize);

    AutoArray<float> raw;
    raw.Resize(totalVoxels);
    std::memcpy(raw.DataPtr(), m_data.DataPtr(), size_t(totalVoxels) * sizeof(float));

    const float* src = raw.DataPtr();
    float* dst = m_data.DataPtr();
    const float invSize = 1.0f / float(m_gridSize);

    for (int z = 0; z < m_gridSize; ++z) {
        float pz = (float(z) + 0.5f) * invSize;
        for (int y = 0; y < m_gridSize; ++y) {
            float py = (float(y) + 0.5f) * invSize;
            for (int x = 0; x < m_gridSize; ++x) {
                float px = (float(x) + 0.5f) * invSize;

                float n = TrilinearSampleWrap(src, m_gridSize, px * 0.3f, py * 0.3f, pz * 0.3f);
                float localWarp = WarpStrength * (0.5f + n);

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


bool BaseCloudNoiseTexture::LoadFromFile(const String& filename) {
    return m_data.LoadFromFile(filename, size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize));
}


bool BaseCloudNoiseTexture::SaveToFile(const String& filename) const {
    return m_data.SaveToFile(filename);
}


// -------------------------------------------------------------------------------------------------
// Mip generation.

void BaseCloudNoiseTexture::DownSample(float* src, int srcEdgeLen, float* dest, int destEdgeLen) {
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


void BaseCloudNoiseTexture::ToMaxMip(BaseCloudNoiseTexture* mipTex) {
    if (mipTex == nullptr)
        return;
    DownSample(m_data.DataPtr(), m_gridSize, mipTex->m_data.DataPtr(), mipTex->m_gridSize);
}


BaseCloudNoiseTexture* BaseCloudNoiseTexture::CreateMaxMip(int mipSize, String noiseFilename) {
    if (not IsPowerOfTwo(m_gridSize))
        return nullptr;
    if (not IsPowerOfTwo(mipSize))
        return nullptr;
    if (mipSize >= m_gridSize)
        return nullptr;

    BaseCloudNoiseTexture* mipTex = NewMaxMipTex();
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


void BaseCloudNoiseTexture::DownSampleAvg(float* src, int srcEdgeLen, float* dest, int destEdgeLen) {
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
    const float invSamples = 1.0f / float(ratio * ratio * ratio);

    for (int zd = 0; zd < destEdgeLen; ++zd) {
        const int sz0 = zd * ratio;
        for (int yd = 0; yd < destEdgeLen; ++yd) {
            const int sy0 = yd * ratio;
            for (int xd = 0; xd < destEdgeLen; ++xd) {
                const int sx0 = xd * ratio;
                float sum = 0.0f;
                for (int iz = 0; iz < ratio; ++iz) {
                    const size_t zOff = size_t(sz0 + iz) * srcSlice;
                    for (int iy = 0; iy < ratio; ++iy) {
                        const size_t yOff = size_t(sy0 + iy) * srcRow;
                        for (int ix = 0; ix < ratio; ++ix) {
                            sum += src[zOff + yOff + size_t(sx0 + ix)];
                        }
                    }
                }
                dest[size_t(zd) * destSlice + size_t(yd) * destRow + size_t(xd)] = sum * invSamples;
            }
        }
    }
}


void BaseCloudNoiseTexture::ToAvgMip(BaseCloudNoiseTexture* mipTex) {
    if (mipTex == nullptr)
        return;
    DownSampleAvg(m_data.DataPtr(), m_gridSize, mipTex->m_data.DataPtr(), mipTex->m_gridSize);
}


BaseCloudNoiseTexture* BaseCloudNoiseTexture::CreateAvgMip(int mipSize, String noiseFilename) {
    if (not IsPowerOfTwo(m_gridSize))
        return nullptr;
    if (not IsPowerOfTwo(mipSize))
        return nullptr;
    if (mipSize >= m_gridSize)
        return nullptr;

    BaseCloudNoiseTexture* mipTex = NewAvgMipTex();
    if (mipTex == nullptr)
        return nullptr;

    if (not mipTex->Create(mipSize, m_params, noiseFilename, false)) {
        delete mipTex;
        return nullptr;
    }

    if (mipTex->IsDeployed())
        return mipTex;

    ToAvgMip(mipTex);
    if (not mipTex->Deploy()) {
        delete mipTex;
        return nullptr;
    }
    mipTex->SaveToFile(noiseFilename);
    return mipTex;
}


// =================================================================================================
// BaseDetailNoiseTexture — 64³ R8 detail Worley-FBM für Schneider-style Edge-Erosion im Shader.

bool BaseDetailNoiseTexture::Allocate(int gridSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_gridSize = gridSize;
    m_data.Resize(BufferSize());
    texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize, 1, 0, 0);
    return true;
}


bool BaseDetailNoiseTexture::Create(int gridSize, const NoiseParams& params,
                                    String noiseFilename, bool compute)
{
    if (not Texture::Create())
        return false;
    SetType(TextureType::Texture3D);
    if (not Allocate(gridSize))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        if (not compute)
            return true;
        std::filesystem::path _p{ noiseFilename.GetStr() };
        Compute(_p.parent_path().string());
        SaveToFile(noiseFilename);
    }
    return Deploy();
}


void BaseDetailNoiseTexture::Compute(String textureFolder) {
    // RGBA-Source mit den drei Worley-Frequenzbaendern in GBA — Perlin-Kanal in R wird nicht
    // genutzt, faellt aber mit ab (akzeptabel, kostet kaum Zeit gegenueber dem Worley-Loop).
    // Normalisierung aller Kanaele, damit die Worley-Verteilung den vollen [0,1]-Range erreicht.
    m_params.normalize = 1 + 2 + 4 + 8;
    NoiseTexture3D rgbaNoise;
    rgbaNoise.Create({ m_gridSize, m_gridSize, m_gridSize }, m_params,
                     textureFolder + "/detailnoise-rgba.bin", false);

    float* rgbaData = rgbaNoise.GetData().DataPtr();
    uint8_t* data = m_data.DataPtr();
    const uint32_t dataSize = BufferSize();

    // Schneider-Standardgewichtung der drei Worley-Frequenzbaender. Summe = 1, also Output ist
    // direkt in [0, 1]. R8-Quantisierung via *255 + Rundung.
    for (uint32_t i = dataSize; i; --i) {
        float worley = rgbaData[1] * 0.625f + rgbaData[2] * 0.25f + rgbaData[3] * 0.125f;
        worley = std::clamp(worley, 0.0f, 1.0f);
        *data++ = static_cast<uint8_t>(worley * 255.0f + 0.5f);
        rgbaData += 4;
    }

    rgbaNoise.GetData().Reset();
}


bool BaseDetailNoiseTexture::LoadFromFile(const String& filename) {
    return m_data.LoadFromFile(filename, BufferSize());
}


bool BaseDetailNoiseTexture::SaveToFile(const String& filename) {
    return m_data.SaveToFile(filename);
}


// =================================================================================================
// BaseBlueNoiseTexture — STBN PNG stack loaded from disk.

bool BaseBlueNoiseTexture::Allocate(void) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_data.Resize(BufferSize());
    texBuf->m_info = TextureBuffer::BufferInfo(m_gridSize.x, m_gridSize.y * 64, 1, 0, 0);
    return true;
}


bool BaseBlueNoiseTexture::Create(String noiseFilename) {
    if (not Texture::Create())
        return false;
    SetType(TextureType::Texture3D);
    if (not Allocate())
        return false;
    if (not LoadFromFile(noiseFilename)) {
        std::filesystem::path _p{ noiseFilename.GetStr() };
        Compute(_p.parent_path().string());
        SaveToFile(noiseFilename);
    }
    return Deploy();
}


void BaseBlueNoiseTexture::Compute(String textureFolder) {
    uint32_t layerSize = m_gridSize.x * m_gridSize.y;
    for (int i = 0; i < 64; ++i) {
        String filename = textureFolder + "/bluenoise/stbn_scalar_2Dx1Dx1D_128x128x64x1_" + String(i) + ".png";
        SDL_Surface* image = IMG_Load(filename.Data());
        if (image) {
            std::memcpy(m_data.DataPtr(layerSize * i), image->pixels, layerSize);
            SDL_FreeSurface(image);
        }
    }
}


bool BaseBlueNoiseTexture::LoadFromFile(const String& filename) {
    return m_data.LoadFromFile(filename, BufferSize());
}


bool BaseBlueNoiseTexture::SaveToFile(const String& filename) {
    return m_data.SaveToFile(filename);
}

// =================================================================================================
