#pragma once

#include "texture.h"
#include "dx12context.h"
#include "descriptor_heap.h"
#include "dx12upload.h"
#include "noise.h"
#include "FBM.h"

// =================================================================================================
// 4 layers of periodic noise: R - Perlin-Worley, GBA: Worley-FBM with doubled frequency for each successive channel
// Tags

struct ValueNoiseR32F {};     // Hash2i, 1 channel, float32
struct FbmNoiseR32F {};       // fbmPeriodic, 1 channel, float32
struct HashNoiseRGBA8 {};     // 4 channels uint8, white tile-noise
struct WeatherNoiseRG8 {};
struct BlueNoiseR8 {};

// -------------------------------------------------------------------------------------------------
// Helpers

static inline int  Wrap(int a, int p) {
    int r = a % p;
    return r < 0 ? r + p : r;
}

static inline uint8_t ToByte01(float v) {
    int iv = (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    return (uint8_t)std::clamp(iv, 0, 255);
}

static inline float Hash2i(int ix, int iy) {
    float t = float(ix) * 127.1f + float(iy) * 311.7f;
    float s = std::sin(t) * 43758.5453f;
    return s - std::floor(s);
}

// -------------------------------------------------------------------------------------------------
// Traits
template<class Tag> struct NoiseTraits;

// 2D value noise (float32, 1 channel)
template<> struct NoiseTraits<ValueNoiseR32F> {
    using PixelT = float;
    static constexpr DXGI_FORMAT dxgiFormat  = DXGI_FORMAT_R32_FLOAT;
    static constexpr uint32_t    pixelStride = 4;   // sizeof(float)
    static constexpr int         Components  = 1;

    // OGL: GL_LINEAR_MIPMAP_LINEAR / GL_LINEAR / GL_REPEAT, glGenerateMipmap.
    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter = GfxFilterMode::Linear;
        s.magFilter = GfxFilterMode::Linear;
        s.mipMode   = GfxMipMode::Linear;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }


    static void Compute(AutoArray<float>& data, int gridSize, int yPeriod, int xPeriod, int /*octave*/, uint32_t /*seed*/) {
        data.Resize(gridSize * gridSize);
        float* dataPtr = data.Data();
        for (int y = 0; y < gridSize; ++y)
            for (int x = 0; x < gridSize; ++x)
                *dataPtr++ = Hash2i(x % xPeriod, y % yPeriod);
    }
};

// -------------------------------------------------------------------------------------------------
// 2D fbm (float32, 1 channel)
template<> struct NoiseTraits<FbmNoiseR32F> {
    using PixelT = float;
    static constexpr DXGI_FORMAT dxgiFormat  = DXGI_FORMAT_R32_FLOAT;
    static constexpr uint32_t    pixelStride = 4;
    static constexpr int         Components  = 1;

    // OGL: GL_LINEAR_MIPMAP_LINEAR / GL_LINEAR / GL_REPEAT, glGenerateMipmap.
    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter = GfxFilterMode::Linear;
        s.magFilter = GfxFilterMode::Linear;
        s.mipMode   = GfxMipMode::Linear;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }


#pragma warning(push)
#pragma warning(disable:4100)
    static void Compute(AutoArray<float>& data, int gridSize, int yPeriod = 1, int xPeriod = 1, int octave = 1) {
#pragma warning(pop)
        // fbmPeriodic path disabled — only stub allocation
        data.Resize(gridSize * gridSize);
    }
};

// -------------------------------------------------------------------------------------------------
// RGBA8: R = Perlin × (1−Worley), G/B/A = Worley-fBm, doubled base frequency per channel
template<> struct NoiseTraits<HashNoiseRGBA8> {
    using PixelT = uint8_t;
    static constexpr DXGI_FORMAT dxgiFormat  = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr uint32_t    pixelStride = 4;
    static constexpr int         Components  = 4;

    // OGL: GL_LINEAR / GL_LINEAR / GL_REPEAT, base/max=0 (no mipmaps).
    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter = GfxFilterMode::Linear;
        s.magFilter = GfxFilterMode::Linear;
        s.mipMode   = GfxMipMode::None;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }


#pragma warning(push)
#pragma warning(disable:4100)
    static void Compute(AutoArray<uint8_t>& data, int gridSize, int /*yPeriod*/, int /*xPeriod*/, int octave, uint32_t seed)
#pragma warning(pop)
    {
        // Disabled — noise computation kept in #if 0 in OGL version
        data.Resize(gridSize * gridSize * 4);
    }
};

// -------------------------------------------------------------------------------------------------

template<>
struct NoiseTraits<WeatherNoiseRG8> {
    using PixelT = uint8_t;
    static constexpr DXGI_FORMAT dxgiFormat  = DXGI_FORMAT_R8G8_UNORM;
    static constexpr uint32_t    pixelStride = 2;
    static constexpr int         Components  = 2;

    // OGL: GL_LINEAR / GL_LINEAR / GL_REPEAT.
    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter = GfxFilterMode::Linear;
        s.magFilter = GfxFilterMode::Linear;
        s.mipMode   = GfxMipMode::None;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }


#pragma warning(push)
#pragma warning(disable:4100)
    static void Compute(AutoArray<uint8_t>& data, int gridSize, int yPeriod, int xPeriod, int octaves)
#pragma warning(pop)
    {
        data.Resize(gridSize * gridSize * 2);
    }
};

// -------------------------------------------------------------------------------------------------

template<> struct NoiseTraits<BlueNoiseR8> {
    using PixelT = uint8_t;
    static constexpr DXGI_FORMAT dxgiFormat  = DXGI_FORMAT_R8_UNORM;
    static constexpr uint32_t    pixelStride = 1;
    static constexpr int         Components  = 1;

    // OGL: GL_NEAREST / GL_NEAREST / GL_REPEAT.
    static void ConfigureSampling(TextureSampling& s) noexcept {
        s.minFilter = GfxFilterMode::Nearest;
        s.magFilter = GfxFilterMode::Nearest;
        s.mipMode   = GfxMipMode::None;
        s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
        s.compareFunc = GfxOperations::CompareFunc::Always;
        s.maxAnisotropy = 1.0f;
    }


    static void Compute(AutoArray<uint8_t>& data, int gridSize, int /*yPeriod*/, int /*xPeriod*/, int /*octaves*/) {
        const int N = gridSize;
        data.Resize(N * N);
        std::vector<float> white(N * N);

        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                uint32_t h = Noise::HashXYC32(x, y, 0x1234567u, 0u);
                float v = (h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
                white[y * N + x] = v;
            }
        }

        uint8_t* dst = data.Data();
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                float sum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    int yy = Wrap(y + dy, N);
                    for (int dx = -1; dx <= 1; ++dx) {
                        int xx = Wrap(x + dx, N);
                        sum += white[yy * N + xx];
                    }
                }
                float m = sum * (1.0f / 9.0f);
                float v = white[y * N + x] - m + 0.5f;
                v = std::clamp(v, 0.0f, 1.0f);
                *dst++ = ToByte01(v);
            }
        }
    }
};

// -------------------------------------------------------------------------------------------------
// Texture-Wrapper — DX12 implementation
template<class Tag>
class NoiseTexture
    : public Texture {
public:
    bool Create(int gridSize, int yPeriod = 1, int xPeriod = 1, int octaves = 1, uint32_t seed = 1) {
        if (not Texture::Create())
            return false;
        if (not Allocate(gridSize))
            return false;
        Compute(gridSize, yPeriod, xPeriod, octaves, seed);
        return Deploy();
    }

    void SetParams(bool /*enforce*/) override {
        m_hasParams = true;
        NoiseTraits<Tag>::ConfigureSampling(m_sampling);
    }

    inline bool IsAvailable(void) noexcept {
        return m_isValid && (m_data.Length() != 0);
    }

    const AutoArray<typename NoiseTraits<Tag>::PixelT>& Data() const { return m_data; }

private:
    AutoArray<typename NoiseTraits<Tag>::PixelT> m_data;

    bool Allocate(int gridSize) {
        auto* texBuf = new TextureBuffer();
        if (not texBuf) 
            return false;
        if (not m_buffers.Append(texBuf)) {
            delete texBuf;
            return false;
        }
        texBuf->m_info = TextureBuffer::BufferInfo(
            gridSize, gridSize,
            NoiseTraits<Tag>::Components,
            0, 0);  // internalFormat/format unused in DX12
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
        if (w <= 0 or h <= 0)
            return false;

        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;

        constexpr DXGI_FORMAT fmt = NoiseTraits<Tag>::dxgiFormat;
        constexpr uint32_t stride = NoiseTraits<Tag>::pixelStride;

        // (Re-)create Texture2D resource
        m_resource.Reset();
        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = UINT(w);
        rd.Height           = UINT(h);
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = fmt;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_resource))))
            return false;
#ifdef _DEBUG
        char name[128];
        snprintf(name, sizeof(name), "NoiseTexture[%s]", (const char*) m_name   );
        m_resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif

        const uint8_t* src = reinterpret_cast<const uint8_t*>(m_data.Data());
        if (not UploadTextureData(device, m_resource.Get(), src, w, h, int(stride)))
            return false;

        // Create / update SRV
        if (m_handle == UINT32_MAX) {
            DescriptorHandle hdl = descriptorHeaps.AllocSRV();
            if (not hdl.IsValid())
                return false;
            m_handle = hdl.index;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                  = fmt;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;

        device->CreateShaderResourceView(m_resource.Get(),
            &srvDesc, descriptorHeaps.m_srvHeap.CpuHandle(m_handle));

        m_isValid = true;
        m_isDeployed = true;
        return true;
    }
};

// -------------------------------------------------------------------------------------------------

using ValueNoiseTexture   = NoiseTexture<ValueNoiseR32F>;
using FbmNoiseTexture     = NoiseTexture<FbmNoiseR32F>;
using HashNoiseTexture    = NoiseTexture<HashNoiseRGBA8>;
using WeatherNoiseTexture = NoiseTexture<WeatherNoiseRG8>;

// =================================================================================================

class NoiseTexture3D
    : public Texture
{
private:
    Vector3i            m_gridDimensions{ 0, 0, 0 };
    NoiseParams         m_params;
    AutoArray<float>    m_data;

public:
    virtual bool Deploy(int bufferIndex = 0) override;

    virtual void SetParams(bool enforce = false) override;

    bool Create(Vector3i gridDimensions, const NoiseParams& params, String noiseFilename = "", bool deploy = true);

    inline AutoArray<float>& GetData(void) noexcept {
        return m_data;
    }

private:
    bool Allocate(Vector3i gridDimensions);

    void ComputeNoise(void);

    bool LoadFromFile(const String& filename);

    bool SaveToFile(const String& filename) const;

    inline uint32_t GridSize(void) const noexcept {
        return uint32_t(m_gridDimensions.x) * uint32_t(m_gridDimensions.y) * uint32_t(m_gridDimensions.z);
    }
};

// =================================================================================================

class CloudNoiseTexture
    : public Texture
{
public:
    virtual bool Deploy(int bufferIndex = 0) override;

    virtual void SetParams(bool enforce = false) override;

    bool Create(int gridSize, const NoiseParams& params, String noiseFilename = "", bool compute = true);

    void ToMaxMip(CloudNoiseTexture* mipTex);

    CloudNoiseTexture* CreateMaxMip(int destSize, String noiseFilename = "");

    static void DownSample(float* src, int srcEdgeLen, float* dest, int destEdgeLen);

private:
    int                 m_gridSize{ 0 };
    NoiseParams         m_params;
    AutoArray<float>    m_data;

    bool Allocate(int gridSize);

    void Compute(String textureFolder = "");

    void ApplyWarp(void);

    void ApplyInfiniteWarp(void);

    void ApplyPeriodicWarp(void);

    bool LoadFromFile(const String& filename);

    bool SaveToFile(const String& filename) const;
};

// =================================================================================================

class NoiseMaxMipTexture
    : public CloudNoiseTexture
{
public:
    virtual void SetParams(bool enforce = false) override;
};

// =================================================================================================

class BlueNoiseTexture
    : public Texture
{
public:
    virtual bool Deploy(int bufferIndex = 0) override;

    virtual void SetParams(bool enforce = false) override;

    bool Create(String noiseFilename = "");

private:
    Vector3i                m_gridSize{ 128, 128, 64 };  // fixed; NVidia STBN data
    AutoArray<uint8_t>      m_data;

    bool Allocate();

    void Compute(String textureFolder = "");

    bool LoadFromFile(const String& filename);

    bool SaveToFile(const String& filename);

    uint32_t BufferSize(void) noexcept {
        return uint32_t(m_gridSize.x * m_gridSize.y * m_gridSize.z);
    }
};

// =================================================================================================
