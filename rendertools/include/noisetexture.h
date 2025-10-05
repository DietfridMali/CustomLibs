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
static inline float Fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
static inline int  Wrap(int a, int p) { int r = a % p; return r < 0 ? r + p : r; }
static inline uint8_t ToByte01(float v) {
    int iv = (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    return (uint8_t)std::clamp(iv, 0, 255);
}
static inline uint32_t Hash3D(uint32_t x, uint32_t y, uint32_t z, uint32_t seed) {
    uint32_t v = x * 0x27d4eb2du ^ (y + 0x9e3779b9u); v ^= z * 0x85ebca6bu ^ seed;
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
    return v;
}
static inline float R01(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); } // [0,1)

// -------------------------------------------------------------
// Periodischer Perlin 3D + fBm
static inline void Grad3(uint32_t h, float& gx, float& gy, float& gz) {
    switch (h % 12u) {
    case 0: gx = 1; gy = 1; gz = 0; break; case 1: gx = -1; gy = 1; gz = 0; break;
    case 2: gx = 1; gy = -1; gz = 0; break; case 3: gx = -1; gy = -1; gz = 0; break;
    case 4: gx = 1; gy = 0; gz = 1; break; case 5: gx = -1; gy = 0; gz = 1; break;
    case 6: gx = 1; gy = 0; gz = -1; break; case 7: gx = -1; gy = 0; gz = -1; break;
    case 8: gx = 0; gy = 1; gz = 1; break; case 9: gx = 0; gy = -1; gz = 1; break;
    case 10: gx = 0; gy = 1; gz = -1; break; default: gx = 0; gy = -1; gz = -1; break;
    }
}

static float Perlin3_Periodic(float x, float y, float z, int P, uint32_t seed) {
    int xi0 = (int)std::floor(x), yi0 = (int)std::floor(y), zi0 = (int)std::floor(z);
    float xf = x - xi0, yf = y - yi0, zf = z - zi0;
    int xi1 = xi0 + 1, yi1 = yi0 + 1, zi1 = zi0 + 1;
    int X0 = Wrap(xi0, P), X1 = Wrap(xi1, P);
    int Y0 = Wrap(yi0, P), Y1 = Wrap(yi1, P);
    int Z0 = Wrap(zi0, P), Z1 = Wrap(zi1, P);

    auto dotg = [&](int ix, int iy, int iz, float dx, float dy, float dz) {
        float gx, gy, gz; Grad3(Hash3D(ix, iy, iz, seed), gx, gy, gz);
        return gx * dx + gy * dy + gz * dz;
        };
    float n000 = dotg(X0, Y0, Z0, xf, yf, zf);
    float n100 = dotg(X1, Y0, Z0, xf - 1, yf, zf);
    float n010 = dotg(X0, Y1, Z0, xf, yf - 1, zf);
    float n110 = dotg(X1, Y1, Z0, xf - 1, yf - 1, zf);
    float n001 = dotg(X0, Y0, Z1, xf, yf, zf - 1);
    float n101 = dotg(X1, Y0, Z1, xf - 1, yf, zf - 1);
    float n011 = dotg(X0, Y1, Z1, xf, yf - 1, zf - 1);
    float n111 = dotg(X1, Y1, Z1, xf - 1, yf - 1, zf - 1);

    float u = Fade(xf), v = Fade(yf), w = Fade(zf);
    float nx00 = n000 + (n100 - n000) * u;
    float nx10 = n010 + (n110 - n010) * u;
    float nx01 = n001 + (n101 - n001) * u;
    float nx11 = n011 + (n111 - n011) * u;
    float nxy0 = nx00 + (nx10 - nx00) * v;
    float nxy1 = nx01 + (nx11 - nx01) * v;
    return nxy0 + (nxy1 - nxy0) * w; // ~[-1,1]
}

static float PerlinFBM3_Periodic(float x, float y, float z, int P, int oct, float lac, float gain, uint32_t seed) {
    float a = 1.f, sum = 0.f, norm = 0.f;
    for (int i = 0; i < oct; ++i) {
        sum += a * Perlin3_Periodic(x, y, z, P, seed + uint32_t(i * 131));
        norm += a; a *= gain; x *= lac; y *= lac; z *= lac; P = int(P * lac);
    }
    return 0.5f + 0.5f * (sum / std::max(norm, 1e-6f)); // [0,1]
}

// -------------------------------------------------------------
// Periodischer Worley 3D (F1) + invertiert + fBm
static float WorleyF1_Periodic(float x, float y, float z, int C, uint32_t seed) {
    int cx = (int)std::floor(x), cy = (int)std::floor(y), cz = (int)std::floor(z);
    float dmin = 1e9f;
    for (int dz = -1; dz <= 1; ++dz) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        int ix = Wrap(cx + dx, C), iy = Wrap(cy + dy, C), iz = Wrap(cz + dz, C);
        uint32_t h = Hash3D((uint32_t)ix, (uint32_t)iy, (uint32_t)iz, seed);
        float jx = R01(h);
        float jy = R01(h * 0x9e3779b9u + 0x7f4a7c15u);
        float jz = R01(h * 0x85ebca6bu + 0xc2b2ae35u);
        float fx = (ix + jx), fy = (iy + jy), fz = (iz + jz);

        float dxw = std::fabs(x - fx); dxw = std::min(dxw, float(C) - dxw);
        float dyw = std::fabs(y - fy); dyw = std::min(dyw, float(C) - dyw);
        float dzw = std::fabs(z - fz); dzw = std::min(dzw, float(C) - dzw);
        float d = std::sqrt(dxw * dxw + dyw * dyw + dzw * dzw);
        dmin = std::min(dmin, d);
    }
    const float invMax = 1.0f / 1.7320508075688772f; // 1/sqrt(3)
    return std::clamp(dmin * invMax, 0.0f, 1.0f);
}

static inline float WorleyInv_Periodic(float x, float y, float z, int C, uint32_t seed) {
    return 1.0f - WorleyF1_Periodic(x, y, z, C, seed);
}

static float WorleyFBM_Periodic(float xP, float yP, float zP, int P, int C0, int oct, float lac, float gain, uint32_t seed) {
    float a = 1.f, sum = 0.f, norm = 0.f;
    float sx = xP * (float)C0 / float(P);
    float sy = yP * (float)C0 / float(P);
    float sz = zP * (float)C0 / float(P);
    int C = C0;
    for (int i = 0; i < oct; ++i) {
        sum += a * WorleyInv_Periodic(sx, sy, sz, C, seed + uint32_t(i * 733));
        norm += a;
        a *= gain; sx *= lac; sy *= lac; sz *= lac; C = int(C * lac);
    }
    return std::clamp(sum / std::max(norm, 1e-6f), 0.0f, 1.0f);
}

// -------------------------------------------------------------
// Optionale 2D-ValueNoise (wie im Original)
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
    int x0 = Wrap(ix0, perX), x1 = Wrap(ix1, perX);
    float a = Hash2i(x0, iy0);
    float b = Hash2i(x1, iy0);
    float c = Hash2i(x0, iy1);
    float d = Hash2i(x1, iy1);
    float ux = Smoothe(fx), uy = Smoothe(fy);
    float ab = a + (b - a) * ux;
    float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy; // 0..1
}

static float fbmPeriodic(float x, float y, int perX, int perY, int octaves = 3, float lac = 2.0f, float gain = 0.5f) {
    float s = 0.0f, amp = 1.0f, norm = 0.0f;
    float px = x, py = y; int pX = perX; (void)perY;
    for (int o = 0; o < octaves; ++o) {
        s += amp * ValueNoisePeriodic(px, py, pX, 0);
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

static inline uint32_t HashXYC32(int x, int y, uint32_t seed, uint32_t ch) {
    uint32_t v = (uint32_t)x;
    v ^= (uint32_t)y * 0x27d4eb2dU;
    v ^= seed * 0x9e3779b9U;
    v ^= (ch + 1U) * 0x85ebca6bU;
    v ^= v >> 16; v *= 0x7feb352dU;
    v ^= v >> 15; v *= 0x846ca68bU;
    v ^= v >> 16; return v;
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

// -------------------------------------------------------------
// Texture-Wrapper wie im Original
template<class Tag>
class NoiseTexture 
    : public Texture {
public:
    bool Create(int edgeSize, int yPeriod = 1, int xPeriod = 1, int octaves = 1, uint32_t seed = 1) {
        if (not Texture::Create()) return false;
        if (not Allocate(edgeSize)) return false;
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

