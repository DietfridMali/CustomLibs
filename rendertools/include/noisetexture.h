#pragma once

#include "texture.h"

// =================================================================================================
// 4 layers of periodic noise: R - Perlin-Worley, GBA: Worley-FBM with doubled frequency for each successive channel
// Tags

struct ValueNoiseR32F {};     // dein aktueller #if 1 Pfad (Hash2i, 1 Kanal)
struct FbmNoiseR32F {};   // dein #else Pfad (fbmPeriodic, 1 Kanal)
struct HashNoiseRGBA8 {};         // neu: 4 Kanäle uint8, weißes Tile-Noise

// -------------------------------------------------------------
// Helpers

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

// -------------------------------------------------------------
// Periodischer Perlin 3D + fBm
float PerlinFBM3_Periodic(float x, float y, float z, int P, int oct, float lac, float gain, uint32_t seed);

float WorleyF1_Periodic(float x, float y, float z, int C, uint32_t seed);

float WorleyFBM_Periodic(float xP, float yP, float zP, int P, int C0, int oct, float lac, float gain, uint32_t seed);

float fbmPeriodic(float x, float y, int perX, int perY, int octaves = 3, float lac = 2.0f, float gain = 0.5f);

inline uint8_t Hash2iByte(int ix, int iy, uint32_t seed, uint32_t ch);

uint32_t HashXYC32(int x, int y, uint32_t seed, uint32_t ch);

static inline float WorleyInv_Periodic(float x, float y, float z, int C, uint32_t seed) {
    return 1.0f - WorleyF1_Periodic(x, y, z, C, seed);
}

// -------------------------------------------------------------
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
    static void Compute(ManagedArray<float>& data, int edgeSize, int yPeriod, int xPeriod, int /*octave*/, uint32_t /*seed*/) {
        data.Resize(edgeSize * edgeSize);
        float* dataPtr = data.Data();
        for (int y = 0; y < edgeSize; ++y)
            for (int x = 0; x < edgeSize; ++x)
                *dataPtr++ = Hash2i(x % xPeriod, y % yPeriod);
    }
};

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
    static void Compute(ManagedArray<float>& data, int edgeSize, int yPeriod = 1, int xPeriod = 1, int octave = 1) {
        data.Resize(edgeSize * edgeSize);
        float* dataPtr = data.Data();
        for (int y = 0; y < edgeSize; ++y) {
            for (int x = 0; x < edgeSize; ++x) {
                float sx = (x + 0.5f) / edgeSize * float(yPeriod);
                float sy = (y + 0.5f) / edgeSize * float(xPeriod);
                float n = fbmPeriodic(sx, sy, yPeriod, xPeriod, octave, 2.0f, 0.5f);
                *dataPtr++ = std::clamp(n, 0.0f, 1.0f);
            }
        }
    }
};

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

    // 'octave' ist hier der z-Slice-Index [0..edgeSize-1]
    static void Compute(ManagedArray<uint8_t>& data, int edgeSize, int /*yPeriod*/, int /*xPeriod*/, int octave, uint32_t seed) {
        data.Resize(edgeSize * edgeSize * 4);
        uint8_t* dataPtr = data.Data();

        const int   P = edgeSize;                 // Periode in allen Achsen
        const float fx = 1.0f / float(edgeSize);
        const float fy = 1.0f / float(edgeSize);
        const float fz = 1.0f / float(edgeSize);
        const float zP = ((octave + 0.5f) * fz) * float(P);

        // Worley-Basiszellen: G, B=2×G, A=2×B
        const int C0 = 16;             // teilt 128
        const int C_G = C0;
        const int C_B = C0 * 2;
        const int C_A = C0 * 4;
        const int C_R = 32;            // für (1−Worley) im R-Kanal

        for (int y = 0; y < edgeSize; ++y) {
            float yP = ((y + 0.5f) * fy) * float(P);
            for (int x = 0; x < edgeSize; ++x) {
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
    }
};

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

    static void Compute(ManagedArray<uint8_t>& data, int edgeSize, int yPeriod, int xPeriod, int octaves) {
        data.Resize(edgeSize * edgeSize * 2);
        uint8_t* dst = data.Data();

        for (int y = 0; y < edgeSize; ++y) {
            for (int x = 0; x < edgeSize; ++x) {
                float u = (x + 0.5f) / edgeSize * xPeriod;
                float v = (y + 0.5f) / edgeSize * yPeriod;

                // Coverage: großskaliges fbm
                float cov = fbmPeriodic(u, v, xPeriod, yPeriod, octaves, 2.0f, 0.5f);
                cov = std::clamp((cov - 0.35f) * 1.6f, 0.0f, 1.0f);

                // HeightBias: unabhängigeres fbm, um vertikale Verteilung zu modifizieren
                float hb = fbmPeriodic(u + 19.3f, v + 7.1f, xPeriod, yPeriod, 3, 2.0f, 0.5f);
                hb = 0.25f + 0.5f * hb; // 0.25..0.75

                *dst++ = ToByte01(cov);
                *dst++ = ToByte01(hb);
            }
        }
    }
};

// -------------------------------------------------------------
// Texture-Wrapper wie im Original
template<class Tag>
class NoiseTexture 
    : public Texture {
public:
    bool Create(int edgeSize, int yPeriod = 1, int xPeriod = 1, int octaves = 1, uint32_t seed = 1) {
        if (not Texture::Create()) 
            return false;
        if (not Allocate(edgeSize)) 
            return false;
        Compute(edgeSize, yPeriod, xPeriod, octaves, seed);
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

    bool Allocate(int edgeSize) {
        auto* texBuf = new TextureBuffer();
        if (not texBuf) return false;
        if (not m_buffers.Append(texBuf)) { delete texBuf; return false; }
        const GLenum IF = NoiseTraits<Tag>::IFmt;
        const GLenum EF = NoiseTraits<Tag>::EFmt;
        texBuf->m_info = TextureBuffer::BufferInfo(edgeSize, edgeSize, 1, IF, EF);
        HasBuffer() = true;
        return true;
    }

    void Compute(int edgeSize, int yPeriod, int xPeriod, int octaves, uint32_t seed) {
        NoiseTraits<Tag>::Compute(m_data, edgeSize, yPeriod, xPeriod, octaves);
    }

    void Deploy(int bufferIndex = 0) {
        if (Bind()) {
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
        }
    }
};

using ValueNoiseTextureR32F = NoiseTexture<ValueNoiseR32F>;
using FbmNoiseTextureR32F = NoiseTexture<FbmNoiseR32F>;
using HashNoiseTextureRGBA8 = NoiseTexture<HashNoiseRGBA8>;
using WeatherNoiseTexture = NoiseTexture<WeatherNoiseRG8>;

// =================================================================================================

struct Noise3DParams {
    uint32_t seed{ 0x1234567u };
    float baseFrequency{ 2.032f };       // Basisfrequenz (wird intern auf ganze Frequenzen gemappt)
    float lacunarity{ 2.0f };          // Lacunarity (>= 1), nur Richtwert
    int   octaves{ 5 };             // Oktaven
    float initialGain{ 0.5f };  // Start-Amplitude
    float gain{ 0.5f };         // Amplitudenfall
    float warp{ 0.10f };        // Domain-Warp Stärke
    float rot_deg{ 11.25f };    // Rotation um Y-Achse
};

class NoiseTexture3D
	: public Texture
{
public:
	virtual void Deploy(int bufferIndex = 0) override;

	virtual void SetParams(bool enforce = false) override;

	bool Create(int edgeSize, const Noise3DParams& params, String noiseFilename = "");

private:
	int             m_edgeSize;
    Noise3DParams   m_params;

	ManagedArray<float>	m_data;

	bool Allocate(int edgeSize);

	void ComputeNoise(void);

    bool LoadFromFile(const String& filename);

    bool SaveToFile(const String& filename) const;
};

// =================================================================================================

struct CloudVolumeParams {
    uint32_t seed{ 0xC0FFEEu };
    int      octaves{ 5 };        // wie fbm_clouds
    float    baseFreq{ 2.032f };  // Startfrequenz
    float    lacunarity{ 2.6434f };
    float    initialGain{ 0.5f };        // Amplitudenabfall
    float    gain{ 0.5f };        // Amplitudenabfall
};

class CloudVolume3D
    : public Texture
{
public:
    virtual void Deploy(int bufferIndex = 0) override;

    virtual void SetParams(bool enforce = false) override;

    bool Create(int edgeSize, const CloudVolumeParams& params, String noiseFilename = "");

private:
    int                 m_edgeSize{ 0 };
    CloudVolumeParams   m_params;
    ManagedArray<float> m_data;

    bool Allocate(int edgeSize);

    void Compute();

    bool LoadFromFile(const String& filename);

    bool SaveToFile(const String& filename) const;
};

// =================================================================================================

