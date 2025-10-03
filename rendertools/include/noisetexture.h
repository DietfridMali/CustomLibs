#pragma once

#include "texture.h"

// =================================================================================================

// -------------------------------------------------------------
// Tags
// -------------------------------------------------------------
struct ValueNoiseR32F {};     // dein aktueller #if 1 Pfad (Hash2i, 1 Kanal)
struct FbmNoiseR32F {};   // dein #else Pfad (fbmPeriodic, 1 Kanal)
struct HashNoiseRGBA8 {};         // neu: 4 Kan‰le uint8, weiﬂes Tile-Noise

// -------------------------------------------------------------
// Utilities 

static inline float Hash2i(int ix, int iy) {
    float t = float(ix) * 127.1f + float(iy) * 311.7f;
    float s = std::sinf(t) * 43758.5453f;
    return s - std::floor(s);
}

static inline float Smoothe(float t) { 
    return t * t * (3.0f - 2.0f * t); 
}

static float ValueNoisePeriodic(float x, float y, int perX, int /*perY*/) {
    int ix0 = (int)std::floor(x), iy0 = (int)std::floor(y);
    float fx = x - ix0, fy = y - iy0;
    int ix1 = ix0 + 1, iy1 = iy0 + 1;
    auto wrap = [](int a, int p) { int r = a % p; return r < 0 ? r + p : r; };
    int x0 = wrap(ix0, perX), x1 = wrap(ix1, perX);
    float a = Hash2i(x0, iy0);
    float b = Hash2i(x1, iy0);
    float c = Hash2i(x0, iy1);
    float d = Hash2i(x1, iy1);
    float ux = Smoothe(fx), uy = Smoothe(fy);
    float ab = a + (b - a) * ux;
    float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy; // 0..1
}

static float fbmPeriodic(float x, float y, int perX, int perY,
    int octaves = 3, float lac = 2.0f, float gain = 0.5f) {
    float s = 0.0f, amp = 1.0f, norm = 0.0f;
    float px = x, py = y;
    int   pX = perX; (void)perY;
    for (int o = 0; o < octaves; ++o) {
        s += amp * ValueNoisePeriodic(px, py, pX, /*perY*/0);
        norm += amp;
        px *= lac; py *= lac; pX *= (int)lac;
        amp *= gain;
    }
    s /= std::max(norm, 1e-6f);
    s = 0.5f + (s - 0.5f) * 2.2f;
    return std::clamp(s, 0.0f, 1.0f);
}

static inline uint8_t Hash2iByte(int ix, int iy, uint32_t seed, uint32_t ch) {
    float phase = float((seed ^ (ch * 0x9E3779B9u)) & 0xFFFFu) * (1.0f / 65536.0f);
    float t = float(ix) * 127.1f + float(iy) * 311.7f + phase;
    float s = std::sinf(t) * 43758.5453f;
    float f = s - std::floor(s);
    int   v = int(f * 255.0f + 0.5f);
    return (uint8_t)std::min(v, 255);
}

// -------------------------------------------------------------

template<class Tag> struct NoiseTraits;

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

// -------------------------------------------------------------

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

// -------------------------------------------------------------

template<> struct NoiseTraits<HashNoiseRGBA8> {
    using PixelT = uint8_t; // 4 Kan‰le hintereinander
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

    static void Compute(ManagedArray<uint8_t>& data, int edgeSize, int /*yPeriod*/, int /*xPeriod*/, int /*octave*/, uint32_t seed) {
        data.Resize(edgeSize * edgeSize * 4);
        uint8_t* dataPtr = data.Data();
        for (int y = 0; y < edgeSize; ++y) {
            int py = (y % edgeSize + edgeSize) % edgeSize;
            for (int x = 0; x < edgeSize; ++x) {
                int px = (x % edgeSize + edgeSize) % edgeSize;
                size_t i = size_t(y) * edgeSize * 4 + size_t(x) * 4;
                *dataPtr++ = Hash2iByte(px, py, seed, 0);
                *dataPtr++ = Hash2iByte(px + 73, py, seed, 1);
                *dataPtr++ = Hash2iByte(px, py + 91, seed, 2);
                *dataPtr++ = Hash2iByte(px + 157, py + 37, seed, 3);
            }
        }
    }
};

// -------------------------------------------------------------

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

    const std::vector<typename NoiseTraits<Tag>::PixelT>& Data() const { 
        return m_data; 
    }

private:
    ManagedArray<typename NoiseTraits<Tag>::PixelT> m_data;

    bool Allocate(int edgeSize) {
        auto* texBuf = new TextureBuffer();
        if (not texBuf) 
            return false;
        if (not m_buffers.Append(texBuf)) { 
            delete texBuf; 
            return false; 
        }
        const GLenum IF = NoiseTraits<Tag>::IFmt;
        const GLenum EF = NoiseTraits<Tag>::EFmt;
        texBuf->m_info = TextureBuffer::BufferInfo(edgeSize, edgeSize, 1, IF, EF);
        HasBuffer() = true;
        return true;
    }

    void Compute(int edgeSize, int yPeriod, int xPeriod, int octaves, uint32_t seed) {
        NoiseTraits<Tag>::Compute(m_data, edgeSize, yPeriod, xPeriod, octaves, seed);
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

// =================================================================================================

class NoiseTexture3D
	: public Texture
{
public:
	virtual void Deploy(int bufferIndex = 0) override;

	virtual void SetParams(bool enforce = false) override;

	bool Create(int edgeSize);

private:
	int m_edgeSize;

	ManagedArray<float>	m_data;

	bool Allocate(int edgeSize);

	void ComputeNoise(void);

};

// =================================================================================================

