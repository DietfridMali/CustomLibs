#pragma once

#include "texture.h"
#include "noise.h"
#include "FBM.h"

// =================================================================================================
// 4 layers of periodic noise: R - Perlin-Worley, GBA: Worley-FBM with doubled frequency for each successive channel
// Tags

struct ValueNoiseR32F {};     // dein aktueller #if 1 Pfad (Hash2i, 1 Kanal)
struct FbmNoiseR32F {};   // dein #else Pfad (fbmPeriodic, 1 Kanal)
struct HashNoiseRGBA8 {};         // neu: 4 Kanäle uint8, weißes Tile-Noise

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

static inline uint32_t Hash3D(uint32_t x, uint32_t y, uint32_t z, uint32_t seed);

// -------------------------------------------------------------------------------------------------
// Traits
template<class Tag> struct NoiseTraits;

// 2D value noise (unverändert)
template<> struct NoiseTraits<ValueNoiseR32F> {
    using PixelT = float;
    static constexpr GLenum IFmt = GL_R32F;
    static constexpr GLenum EFmt = GL_RED;
    static constexpr GLenum Type = GL_FLOAT;
    static constexpr int    Components = 1;

    static void SetParams(GLenum target) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenerateMipmap(target);
    }
    static void Compute(ManagedArray<float>& data, int gridSize, int yPeriod, int xPeriod, int /*octave*/, uint32_t /*seed*/) {
        data.Resize(gridSize * gridSize);
        float* dataPtr = data.Data();
        for (int y = 0; y < gridSize; ++y)
            for (int x = 0; x < gridSize; ++x)
                *dataPtr++ = Hash2i(x % xPeriod, y % yPeriod);
    }
};

// -------------------------------------------------------------------------------------------------
// 2D fbm (unverändert)
template<> struct NoiseTraits<FbmNoiseR32F> {
    using PixelT = float;
    static constexpr GLenum IFmt = GL_R32F;
    static constexpr GLenum EFmt = GL_RED;
    static constexpr GLenum Type = GL_FLOAT;
    static constexpr int    Components = 1;

    static void SetParams(GLenum target) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenerateMipmap(target);
    }
    static void Compute(ManagedArray<float>& data, int gridSize, int yPeriod = 1, int xPeriod = 1, int octave = 1) {
        data.Resize(gridSize * gridSize);
        float* dataPtr = data.Data();
        for (int y = 0; y < gridSize; ++y) {
            for (int x = 0; x < gridSize; ++x) {
                float sx = (x + 0.5f) / gridSize * float(yPeriod);
                float sy = (y + 0.5f) / gridSize * float(xPeriod);
                //float n = fbmPeriodic(sx, sy, yPeriod, xPeriod, octave, 2.0f, 0.5f);
                //*dataPtr++ = std::clamp(n, 0.0f, 1.0f);
            }
        }
    }
};

// -------------------------------------------------------------------------------------------------
// RGBA8: R = Perlin × (1−Worley), G/B/A = Worley-fBm, je Kanal doppelte Grundfrequenz
template<> struct NoiseTraits<HashNoiseRGBA8> {
    using PixelT = uint8_t;
    static constexpr GLenum IFmt = GL_RGBA8;
    static constexpr GLenum EFmt = GL_RGBA;
    static constexpr GLenum Type = GL_UNSIGNED_BYTE;
    static constexpr int    Components = 4;

    static void SetParams(GLenum target) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
    }

    // 'octave' ist hier der z-Slice-Index [0..gridSize-1]
    static void Compute(ManagedArray<uint8_t>& data, int gridSize, int /*yPeriod*/, int /*xPeriod*/, int octave, uint32_t seed) {
        data.Resize(gridSize * gridSize * 4);
        uint8_t* dataPtr = data.Data();

        const int   P = gridSize;                 // Periode in allen Achsen
        const float fx = 1.0f / float(gridSize);
        const float fy = 1.0f / float(gridSize);
        const float fz = 1.0f / float(gridSize);
        const float zP = ((octave + 0.5f) * fz) * float(P);

        // Worley-Basiszellen: G, B=2×G, A=2×B
        const int C0 = 16;             // teilt 128
        const int C_G = C0;
        const int C_B = C0 * 2;
        const int C_A = C0 * 4;
        const int C_R = 32;            // für (1−Worley) im R-Kanal
#if 0
        for (int y = 0; y < gridSize; ++y) {
            float yP = ((y + 0.5f) * fy) * float(P);
            for (int x = 0; x < gridSize; ++x) {
                float xP = ((x + 0.5f) * fx) * float(P);
                // R: Perlin × (1 − Worley)
                float per = PerlinFBM3_Periodic(xP, yP, zP, P, 1, 2.0f, 0.5f, seed ^ 0xA5A5A5u);
                float wR = WorleyInv_Periodic(xP * (float)C_R / float(P),
                    yP * (float)C_R / float(P),
                    zP * (float)C_R / float(P), C_R, seed ^ 0x51u);
                float R = std::clamp(per * wR, 0.0f, 1.0f);

                // G/B/A: Worley-fBm mit doppelnder Grundfrequenz je Kanal
                float G = WorleyFBM_Periodic(xP, yP, zP, P, C_G, 4, 2.0f, 0.5f, seed ^ 0x1337u);
                float B = WorleyFBM_Periodic(xP, yP, zP, P, C_B, 4, 2.0f, 0.5f, seed ^ 0xBEEFu);
                float A = WorleyFBM_Periodic(xP, yP, zP, P, C_A, 4, 2.0f, 0.5f, seed ^ 0xCAFEu);

                *dataPtr++ = ToByte01(R);
                *dataPtr++ = ToByte01(G);
                *dataPtr++ = ToByte01(B);
                *dataPtr++ = ToByte01(A);
            }
        }
#endif
    }
};

// -------------------------------------------------------------------------------------------------

struct WeatherNoiseRG8 {};

template<>
struct NoiseTraits<WeatherNoiseRG8> {
    using PixelT = uint8_t;
    static constexpr GLenum IFmt = GL_RG8;
    static constexpr GLenum EFmt = GL_RG;
    static constexpr GLenum Type = GL_UNSIGNED_BYTE;
    static constexpr int    Components = 2;

    static void SetParams(GLenum target) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    static void Compute(ManagedArray<uint8_t>& data, int gridSize, int yPeriod, int xPeriod, int octaves) {
        data.Resize(gridSize * gridSize * 2);
        uint8_t* dst = data.Data();

        for (int y = 0; y < gridSize; ++y) {
            for (int x = 0; x < gridSize; ++x) {
                float u = (x + 0.5f) / gridSize * xPeriod;
                float v = (y + 0.5f) / gridSize * yPeriod;
#if 0
                // Coverage: großskaliges fbm
                float cov = fbmPeriodic(u, v, xPeriod, yPeriod, octaves, 2.0f, 0.5f);
                cov = std::clamp((cov - 0.35f) * 1.6f, 0.0f, 1.0f);

                // HeightBias: unabhängigeres fbm, um vertikale Verteilung zu modifizieren
                float hb = fbmPeriodic(u + 19.3f, v + 7.1f, xPeriod, yPeriod, 3, 2.0f, 0.5f);
                hb = 0.25f + 0.5f * hb; // 0.25..0.75

                *dst++ = ToByte01(cov);
                *dst++ = ToByte01(hb);
#endif
            }
        }
    }
};

// -------------------------------------------------------------------------------------------------

struct BlueNoiseR8 {};

template<> struct NoiseTraits<BlueNoiseR8> {
    using PixelT = uint8_t;
    static constexpr GLenum IFmt = GL_R8;
    static constexpr GLenum EFmt = GL_RED;
    static constexpr GLenum Type = GL_UNSIGNED_BYTE;
    static constexpr int    Components = 1;

    static void SetParams(GLenum target) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    static void Compute(ManagedArray<uint8_t>& data, int gridSize, int /*yPeriod*/, int /*xPeriod*/, int /*octaves*/) {
        const int N = gridSize;
        data.Resize(N * N);
        // Zwischenspeicher für White Noise
        std::vector<float> white(N * N);

        // White Noise (periodisch durch Wrap in Hash)
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                // Pseudozufall über vorhandenen Hash
                uint32_t h = Noise::HashXYC32(x, y, 0x1234567u, 0u);
                float v = (h & 0x00FFFFFFu) * (1.0f / 16777216.0f); // [0,1)
                white[y * N + x] = v;
            }
        }

        // Lokaler Highpass: Wert - lokaler Mittelwert (3x3) + 0.5
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
                float v = white[y * N + x] - m + 0.5f; // Highpass + Rezentrierung
                v = std::clamp(v, 0.0f, 1.0f);
                *dst++ = ToByte01(v);
            }
        }
    }
};

// -------------------------------------------------------------------------------------------------
// Texture-Wrapper wie im Original
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
        Deploy();
        return true;
    }

    void SetParams(bool enforce) {
        if (enforce || not m_hasParams) {
            m_hasParams = true;
            NoiseTraits<Tag>::SetParams(GL_TEXTURE_2D);
        }
    }

    const std::vector<typename NoiseTraits<Tag>::PixelT>& Data() const { return m_data; }

    inline bool IsAvailable(void) noexcept {
        return HasBuffer() and (m_data.Length() != 0);
    }

private:
    ManagedArray<typename NoiseTraits<Tag>::PixelT> m_data;

    bool Allocate(int gridSize) {
        auto* texBuf = new TextureBuffer();
        if (not texBuf) return false;
        if (not m_buffers.Append(texBuf)) { delete texBuf; return false; }
        const GLenum IF = NoiseTraits<Tag>::IFmt;
        const GLenum EF = NoiseTraits<Tag>::EFmt;
        texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize, 1, IF, EF);
        HasBuffer() = true;
        return true;
    }

    void Compute(int gridSize, int yPeriod, int xPeriod, int octaves, uint32_t seed) {
        NoiseTraits<Tag>::Compute(m_data, gridSize, yPeriod, xPeriod, octaves);
    }

    bool Deploy(int bufferIndex = 0) {
        if (not Bind())
            return false;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        TextureBuffer* texBuf = m_buffers[bufferIndex];
        glTexImage2D(GL_TEXTURE_2D, 0,
            NoiseTraits<Tag>::IFmt,
            texBuf->Width(), texBuf->Height(), 0,
            NoiseTraits<Tag>::EFmt,
            NoiseTraits<Tag>::Type,
            reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        Release();
        return true;
    }

};

// -------------------------------------------------------------------------------------------------

using ValueNoiseTexture = NoiseTexture<ValueNoiseR32F>;
using FbmNoiseTexture = NoiseTexture<FbmNoiseR32F>;
using HashNoiseTexture = NoiseTexture<HashNoiseRGBA8>;
using WeatherNoiseTexture = NoiseTexture<WeatherNoiseRG8>;

// =================================================================================================

class NoiseTexture3D
	: public Texture
{
private:
    Vector3i    m_gridDimensions;
    NoiseParams m_params;
    ManagedArray<float>	m_data;

public:
	virtual bool Deploy(int bufferIndex = 0) override;

	virtual void SetParams(bool enforce = false) override;

	bool Create(Vector3i gridDimensions, const NoiseParams& params, String noiseFilename = "", bool deploy = true);

    inline ManagedArray<float>& GetData(void) noexcept {
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

    bool Create(int gridSize, const NoiseParams& params, String noiseFilename = "");

private:
    int                 m_gridSize{ 0 };
    NoiseParams         m_params;
    ManagedArray<float> m_data;

    bool Allocate(int gridSize);

    void Compute(String textureFolder = "");

    bool LoadFromFile(const String& filename);

    bool SaveToFile(const String& filename) const;
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
    Vector3i                m_gridSize{ 128, 128, 64 }; // fixed; using NVidia STBN data
    ManagedArray<uint8_t>   m_data;

    bool Allocate();

    void Compute(String textureFolder = "");

    bool LoadFromFile(const String& filename);

    bool SaveToFile(const String& filename);

    uint32_t BufferSize(void) noexcept {
        return uint32_t(m_gridSize.x * m_gridSize.y * m_gridSize.z);
    }
};

// =================================================================================================
