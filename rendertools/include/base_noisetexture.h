#pragma once

#include "texture.h"        // resolved per-backend via include path: opengl / vulkan / directx
#include "rendertypes.h"
#include "texturesampling.h"
#include "noise.h"
#include "FBM.h"
#include "array.hpp"
#include "string.hpp"
#include "vector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// =================================================================================================
// base_noisetexture — API-neutral base classes + tag/template machinery used by the per-backend
// noisetexture units. All CPU-side logic (allocation bookkeeping, noise generation, FBM/Worley
// mixing, warp prebake, mip downsampling, file I/O) lives in this header + base_noisetexture.cpp.
//
// The two operations that genuinely diverge per backend — GPU upload (Deploy) and sampler-state
// configuration (SetParams) — stay virtual and are implemented in <api>/src/noisetexture.cpp.
//
// Upload helpers per backend (Upload2DTexture / Upload3DTexture) carry a GfxPixelFormat value; the
// per-backend mapper (ToGLFormat / ToVkFormat / ToDXGIFormat) translates that to its native
// format identifier at the upload call site.

// =================================================================================================
// Local helpers — non-static so subsequent files can pick them up, namespaced to avoid pollution.

namespace NoiseTextureUtil {

    inline int Wrap(int a, int p) noexcept {
        int r = a % p;
        return r < 0 ? r + p : r;
    }

    inline uint8_t ToByte01(float v) noexcept {
        int iv = int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        return (uint8_t) std::clamp(iv, 0, 255);
    }

    inline float Hash2i(int ix, int iy) noexcept {
        float t = float(ix) * 127.1f + float(iy) * 311.7f;
        float s = std::sin(t) * 43758.5453f;
        return s - std::floor(s);
    }
}

// =================================================================================================
// Tags + traits for the 2D-noise template. Each tag carries (a) the storage GfxPixelFormat, (b)
// the CPU-side pixel type, (c) the component count (informational, used by Allocate), (d) a
// ConfigureSampling routine for the sampler struct, (e) a Compute routine that fills an
// AutoArray<PixelT> with the generated noise.

struct ValueNoiseR32F {};
struct PerlinNoiseR32F {};
struct FbmNoiseR32F {};
struct HashNoiseRGBA8 {};
struct WeatherNoiseRG8 {};
struct BlueNoiseR8 {};

template<class Tag> struct NoiseTraits;

// -------------------------------------------------------------------------------------------------
// 2D value noise

template<> struct NoiseTraits<ValueNoiseR32F> {
    using PixelT = float;
    static constexpr GfxPixelFormat format = GfxPixelFormat::R32_SFloat;
    static constexpr int Components = 1;

    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter = GfxFilterMode::Linear;
        s.magFilter = GfxFilterMode::Linear;
        s.mipMode = GfxMipMode::Linear;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }

    static void Compute(AutoArray<float>& data, int gridSize, int yPeriod, int xPeriod,
                        int /*octave*/, uint32_t /*seed*/)
    {
        data.Resize(gridSize * gridSize);
        float* dataPtr = data.Data();
        for (int y = 0; y < gridSize; ++y)
            for (int x = 0; x < gridSize; ++x)
                *dataPtr++ = NoiseTextureUtil::Hash2i(x % xPeriod, y % yPeriod);
    }
};

// -------------------------------------------------------------------------------------------------
// 2D Perlin noise (tileable when xPeriod >= 2 and yPeriod >= 2)

template<> struct NoiseTraits<PerlinNoiseR32F> {
    using PixelT = float;
    static constexpr GfxPixelFormat format = GfxPixelFormat::R32_SFloat;
    static constexpr int Components = 1;

    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter     = GfxFilterMode::Linear;
        s.magFilter     = GfxFilterMode::Linear;
        s.mipMode       = GfxMipMode::Linear;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc   = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }

    static void Compute(AutoArray<float>& data, int gridSize, int yPeriod, int xPeriod,
                        int /*octave*/, uint32_t seed)
    {
        data.Resize(gridSize * gridSize);
        Noise::PerlinNoise perlin;
        const int period = (xPeriod > yPeriod) ? xPeriod : yPeriod;
        perlin.Setup((period < 2) ? 2 : period, seed);
        float* dataPtr = data.Data();
        const float invGrid = 1.0f / float(gridSize);
        for (int y = 0; y < gridSize; ++y) {
            for (int x = 0; x < gridSize; ++x) {
                Vector3f p(float(x) * invGrid * float(xPeriod),
                           float(y) * invGrid * float(yPeriod),
                           0.0f);
                const float n = perlin.Compute(p);
                *dataPtr++ = n * 0.5f + 0.5f; // [-1,1] -> [0,1]
            }
        }
    }
};

// -------------------------------------------------------------------------------------------------
// 2D fbm (multi-octave Perlin via FBM<PerlinFunctor>)

template<> struct NoiseTraits<FbmNoiseR32F> {
    using PixelT = float;
    static constexpr GfxPixelFormat format = GfxPixelFormat::R32_SFloat;
    static constexpr int Components = 1;

    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter     = GfxFilterMode::Linear;
        s.magFilter     = GfxFilterMode::Linear;
        s.mipMode       = GfxMipMode::Linear;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc   = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }

    static void Compute(AutoArray<float>& data, int gridSize, int yPeriod, int xPeriod,
                        int octaves, uint32_t seed)
    {
        data.Resize(gridSize * gridSize);
        const int period = (xPeriod > yPeriod) ? xPeriod : yPeriod;
        Noise::PerlinFunctor functor;
        functor.generator.Setup((period < 2) ? 2 : period, seed);
        FBMParams params;
        params.octaves = (octaves < 1) ? 1 : octaves;
        FBM<Noise::PerlinFunctor> fbm(functor, params);
        float* dataPtr = data.Data();
        const float invGrid = 1.0f / float(gridSize);
        for (int y = 0; y < gridSize; ++y) {
            for (int x = 0; x < gridSize; ++x) {
                Vector3f p(float(x) * invGrid * float(xPeriod),
                           float(y) * invGrid * float(yPeriod),
                           0.0f);
                *dataPtr++ = fbm.Value(p);
            }
        }
    }
};

// -------------------------------------------------------------------------------------------------
// HashNoise RGBA8 — compute body kept disabled (#if 0 in OGL original); the Tag exists for
// completeness so the using-alias below still names a valid type.

template<> struct NoiseTraits<HashNoiseRGBA8> {
    using PixelT = uint8_t;
    static constexpr GfxPixelFormat format = GfxPixelFormat::RGBA8_UNorm;
    static constexpr int Components = 4;

    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter     = GfxFilterMode::Linear;
        s.magFilter     = GfxFilterMode::Linear;
        s.mipMode       = GfxMipMode::None;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc   = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }

#pragma warning(push)
#pragma warning(disable:4100)
    static void Compute(AutoArray<uint8_t>& data, int gridSize, int /*yPeriod*/, int /*xPeriod*/,
                        int octave, uint32_t seed)
#pragma warning(pop)
    {
        data.Resize(size_t(gridSize) * size_t(gridSize) * 4);
    }
};

// -------------------------------------------------------------------------------------------------
// WeatherNoise RG8 — compute body kept disabled (#if 0 in OGL original)

template<> struct NoiseTraits<WeatherNoiseRG8> {
    using PixelT = uint8_t;
    static constexpr GfxPixelFormat format = GfxPixelFormat::RG8_UNorm;
    static constexpr int Components = 2;

    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter     = GfxFilterMode::Linear;
        s.magFilter     = GfxFilterMode::Linear;
        s.mipMode       = GfxMipMode::None;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc   = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }

#pragma warning(push)
#pragma warning(disable:4100)
    static void Compute(AutoArray<uint8_t>& data, int gridSize, int yPeriod, int xPeriod, int octaves)
#pragma warning(pop)
    {
        data.Resize(size_t(gridSize) * size_t(gridSize) * 2);
    }
};

// -------------------------------------------------------------------------------------------------
// BlueNoise R8 — 2D variant (CPU-generated highpass white noise). Separate from the BlueNoiseTexture
// class below, which handles the STBN 3D blue-noise stack loaded from PNG.

template<> struct NoiseTraits<BlueNoiseR8> {
    using PixelT = uint8_t;
    static constexpr GfxPixelFormat format = GfxPixelFormat::R8_UNorm;
    static constexpr int Components = 1;

    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter     = GfxFilterMode::Nearest;
        s.magFilter     = GfxFilterMode::Nearest;
        s.mipMode       = GfxMipMode::None;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc   = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }

    static void Compute(AutoArray<uint8_t>& data, int gridSize, int /*yPeriod*/, int /*xPeriod*/,
                        int /*octaves*/)
    {
        const int N = gridSize;
        data.Resize(size_t(N) * size_t(N));
        std::vector<float> white(size_t(N) * size_t(N));

        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                uint32_t h = Noise::HashXYC32(x, y, 0x1234567u, 0u);
                float v = (h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
                white[size_t(y) * N + x] = v;
            }
        }

        uint8_t* dst = data.Data();
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                float sum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    int yy = NoiseTextureUtil::Wrap(y + dy, N);
                    for (int dx = -1; dx <= 1; ++dx) {
                        int xx = NoiseTextureUtil::Wrap(x + dx, N);
                        sum += white[size_t(yy) * N + xx];
                    }
                }
                float m = sum * (1.0f / 9.0f);
                float v = white[size_t(y) * N + x] - m + 0.5f;
                v = std::clamp(v, 0.0f, 1.0f);
                *dst++ = NoiseTextureUtil::ToByte01(v);
            }
        }
    }
};

// =================================================================================================
// Forward declarations for the per-backend upload helpers used by Deploy below. The actual
// signatures live in oglupload.h / vkupload.h / dx12upload.h and are picked up from the per-API
// include of those headers transitively (every per-API noisetexture.h pulls its upload header).

bool Upload2DTexture(Texture& tex, int width, int height, GfxPixelFormat fmt, const void* data) noexcept;

bool Upload3DTexture(Texture& tex, int width, int height, int depth, GfxPixelFormat fmt, const void* data, bool generateMips = false) noexcept;

// =================================================================================================
// 2D-noise template — Deploy + SetParams are common; the per-backend upload helper does the work.

template<class Tag>
class BaseNoiseTexture
    : public Texture
{
public:
    bool Create(int gridSize, int yPeriod = 1, int xPeriod = 1, int octaves = 1, uint32_t seed = 1) {
        if (not Texture::Create())
            return false;
        SetType(TextureType::Texture2D);
        if (not Allocate(gridSize))
            return false;
        Compute(gridSize, yPeriod, xPeriod, octaves, seed);
        return Deploy();
    }

    // The actual sampler-state application differs per backend: OGL writes via glTexParameteri,
    // VK/DX write to m_sampling on Texture. m_sampling only exists on the VK/DX Texture base
    // (OGL's Texture has no such field), so writing it here would fail to compile when this
    // template is instantiated under the OGL include path. We keep this body trivial and let the
    // per-backend NoiseTexture<Tag> subclass override SetParams accordingly.
    void SetParams(bool enforce) override {
        if (enforce or not m_hasParams) {
            m_hasParams = true;
        }
    }

    inline const AutoArray<typename NoiseTraits<Tag>::PixelT>& Data() const noexcept {
        return m_data;
    }

    inline bool IsAvailable(void) noexcept {
        return m_isValid && (m_data.Length() != 0);
    }

protected:
    AutoArray<typename NoiseTraits<Tag>::PixelT> m_data;

    bool Allocate(int gridSize) {
        auto* texBuf = new TextureBuffer();
        if (not texBuf)
            return false;
        if (not m_buffers.Append(texBuf)) {
            delete texBuf;
            return false;
        }
        texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize,
                                                   NoiseTraits<Tag>::Components, 0, 0);
        return true;
    }

    void Compute(int gridSize, int yPeriod, int xPeriod, int octaves, uint32_t seed) {
        NoiseTraits<Tag>::Compute(m_data, gridSize, yPeriod, xPeriod, octaves, seed);
    }

    bool Deploy(int /*bufferIndex*/ = 0) override {
        if (m_buffers.IsEmpty() or m_data.IsEmpty())
            return false;
        TextureBuffer* texBuf = m_buffers[0];
        const int w = texBuf->m_info.m_width;
        const int h = texBuf->m_info.m_height;
        return Upload2DTexture(*this, w, h, NoiseTraits<Tag>::format,
                               reinterpret_cast<const void*>(m_data.Data()));
    }
};

// SetParams is overridden by the OpenGL-side NoiseTexture subclass to issue
// glTexParameteri directly — for OpenGL the m_sampling write here is a no-op since the OGL
// Texture base lacks an m_sampling member. VK/DX inherit the templated SetParams unchanged.
//
// Specialised below by including the per-API noisetexture.h, which adds a thin
// `template<class Tag> class NoiseTexture : public BaseNoiseTexture<Tag> { ... };` and supplies the
// OpenGL SetParams body.

// =================================================================================================
// 3D textures — non-template base classes. Deploy + SetParams stay virtual; the per-API subclass
// supplies them. Everything else (Allocate, Compute, ApplyWarp, mip downsampling, file I/O) lives
// in base_noisetexture.cpp.

class BaseNoiseTexture3D
    : public Texture
{
public:
    bool Create(Vector3i gridDimensions, const NoiseParams& params,
                String noiseFilename = "", bool deploy = true);

    inline AutoArray<float>& GetData(void) noexcept {
        return m_data;
    }

    // Per-backend NoiseTexture3D supplies these.
    bool Deploy(int bufferIndex = 0) override = 0;
    void SetParams(bool enforce = false) override = 0;

protected:
    Vector3i         m_gridDimensions{ 0, 0, 0 };
    NoiseParams      m_params;
    AutoArray<float> m_data;

    bool Allocate(Vector3i gridDimensions);
    void ComputeNoise(void);
    bool LoadFromFile(const String& filename);
    bool SaveToFile(const String& filename) const;

    inline uint32_t GridSize(void) const noexcept {
        return uint32_t(m_gridDimensions.x) * uint32_t(m_gridDimensions.y) * uint32_t(m_gridDimensions.z);
    }
};

// =================================================================================================
// Cloud-shape noise — 3D, single-channel float. Combines an RGBA noise (loaded/generated through
// a temporary BaseNoiseTexture3D) into the final R-channel storage. Mip pyramids (max + avg) are
// generated CPU-side; the per-backend subclass overrides only Deploy + SetParams and supplies the
// concrete mip-texture classes via the NewMaxMipTex / NewAvgMipTex factories.

class BaseCloudNoiseTexture
    : public Texture
{
public:
    bool Create(int gridSize, const NoiseParams& params, String noiseFilename = "", bool compute = true);

    void ToMaxMip(BaseCloudNoiseTexture* mipTex);
    
    BaseCloudNoiseTexture* CreateMaxMip(int destSize, String noiseFilename = "");
    
    static void DownSample(float* src, int srcEdgeLen, float* dest, int destEdgeLen);

    void ToAvgMip(BaseCloudNoiseTexture* mipTex);
    
    BaseCloudNoiseTexture* CreateAvgMip(int destSize, String noiseFilename = "");
    
    static void DownSampleAvg(float* src, int srcEdgeLen, float* dest, int destEdgeLen);

    bool Deploy(int bufferIndex = 0) override = 0;
    
    void SetParams(bool enforce = false) override = 0;

protected:
    int              m_gridSize{ 0 };
    NoiseParams      m_params;
    AutoArray<float> m_data;

    bool Allocate(int gridSize);
    
    void Compute(String textureFolder = "");
    
    void ApplyWarp(void);
    
    void ApplyInfiniteWarp(void);
    
    void ApplyPeriodicWarp(void);
    
    bool LoadFromFile(const String& filename);
    
    bool SaveToFile(const String& filename) const;

    // Factories — per-backend subclass returns its concrete mip subclass instance.
    virtual BaseCloudNoiseTexture* NewMaxMipTex(void) = 0;
    
    virtual BaseCloudNoiseTexture* NewAvgMipTex(void) = 0;
};

// =================================================================================================
// DetailNoiseTexture — 64³ R8 detail Worley-FBM für Edge-Erosion im Cloud-Shader.
// Erzeugt intern eine temporaere RGBA NoiseTexture3D (genau wie BaseCloudNoiseTexture::Compute) und
// schreibt die gewichtete Aggregation der drei Worley-Frequenzkanaele (Gewichte 0.625/0.25/0.125,
// identisch zur Schneider-Pipeline) als pre-baked R8 in die finale Textur. Speicher: 64³ × 1 Byte
// = 256 KB. Im Shader sampled das Detail bei hoeherer UV-Frequenz als die Base-Shape und wird an
// Wolken-Edges subtrahiert.

class BaseDetailNoiseTexture
    : public Texture
{
public:
    bool Create(int gridSize, const NoiseParams& params,
                String noiseFilename = "", bool compute = true);

    bool Deploy(int bufferIndex = 0) override = 0;
    void SetParams(bool enforce = false) override = 0;

protected:
    int                m_gridSize{ 0 };
    NoiseParams        m_params;
    AutoArray<uint8_t> m_data;

    bool Allocate(int gridSize);
    void Compute(String textureFolder = "");
    bool LoadFromFile(const String& filename);
    bool SaveToFile(const String& filename);

    inline uint32_t BufferSize(void) const noexcept {
        return uint32_t(m_gridSize) * uint32_t(m_gridSize) * uint32_t(m_gridSize);
    }
};

// =================================================================================================
// BlueNoiseTexture — 3D stack of NVidia STBN PNG slices.

class BaseBlueNoiseTexture
    : public Texture
{
public:
    bool Create(String noiseFilename = "");

    bool Deploy(int bufferIndex = 0) override = 0;
    void SetParams(bool enforce = false) override = 0;

protected:
    Vector3i           m_gridSize{ 128, 128, 64 };
    AutoArray<uint8_t> m_data;

    bool Allocate(void);
    void Compute(String textureFolder = "");
    bool LoadFromFile(const String& filename);
    bool SaveToFile(const String& filename);

    inline uint32_t BufferSize(void) noexcept {
        return uint32_t(m_gridSize.x * m_gridSize.y * m_gridSize.z);
    }
};

// =================================================================================================
