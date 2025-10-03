
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "noisetexture.h"
#include "base_renderer.h"

// =================================================================================================

static inline float Hash2i(int ix, int iy) {
    // GLSL-kompatibel: fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453)
    // seed wirkt nur als kleine Phasenverschiebung
    float t = float(ix) * 127.1f + float(iy) * 311.7f;
    float s = std::sinf(t) * 43758.5453f;
    return s - std::floor(s); // fract
}

static inline float Smoothe(float t) {
    return t * t * (3.0f - 2.0f * t); 
}


static float ValueNoisePeriodic(float x, float y, int perX, int /*perY*/) {
    // U-periodisch (wrap nur X), V aperiodisch (Y ungewrappt) – wie im GLSL valueNoiseUWrap
    int ix0 = (int)std::floor(x), iy0 = (int)std::floor(y);
    float fx = x - ix0, fy = y - iy0;
    int ix1 = ix0 + 1, iy1 = iy0 + 1;

    auto wrap = [](int a, int p) { int r = a % p; return r < 0 ? r + p : r; };
    int x0 = wrap(ix0, perX), 
        x1 = wrap(ix1, perX); // nur X wrappen

    float a = Hash2i(x0, iy0);
    float b = Hash2i(x1, iy0);
    float c = Hash2i(x0, iy1);
    float d = Hash2i(x1, iy1);

    float ux = Smoothe(fx), 
          uy = Smoothe(fy);
    float ab = a + (b - a) * ux;
    float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy; // 0..1
}

static float fbmPeriodic(float x, float y, int perX, int perY, int octaves = 3, float lacunarity = 2.0f, float gain = 0.5f) {
    // Frequenz steigt in X und Y; Periodizität nur in U/X erhalten
    float s = 0.0f, amp = 1.0f, norm = 0.0f;
    float px = x, py = y;
    int   pX = perX;            // nur X-Periode skaliert mit
    (void)perY;                 // V/Y bleibt aperiodisch

    for (int o = 0; o < octaves; ++o) {
        s += amp * ValueNoisePeriodic(px, py, pX, /*perY*/0);
        norm += amp;
        px *= lacunarity;     // höhere Frequenz in X
        py *= lacunarity;     // höhere Frequenz in Y (ohne Wrap)
        pX *= (int)lacunarity; // nur X-Periode verdoppeln
        amp *= gain;
    }
    s /= std::max(norm, 1e-6f);
    s = 0.5f + (s - 0.5f) * 2.2f; // Kontrast wie im Shader
    return std::clamp(s, 0.0f, 1.0f);
}


void NoiseTexture::ComputeNoise(int edgeSize, int yPeriod, int xPeriod, int octaves, uint32_t seed) {
    float* data = m_data.Data();
    for (int y = 0; y < edgeSize; ++y) {
        for (int x = 0; x < edgeSize; ++x) {
            // sample-Koords in Periodenraum
#if 1
            data[y * edgeSize + x] = Hash2i(x % xPeriod, y % yPeriod);
#else
            float sx = (x + 0.5f) / edgeSize * yPeriod;
            float sy = (y + 0.5f) / edgeSize * xPeriod;
            float n = fbmPeriodic(sx, sy, yPeriod, xPeriod, octaves, 2.0f, 0.5f);
            data[y * edgeSize + x] = std::clamp(n, 0.0f, 1.0f);
#endif
        }
    }
}

    
bool NoiseTexture::Allocate(int edgeSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (!texBuf)
        return false;
    if (!m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_data.Resize(edgeSize * edgeSize);
    texBuf->m_info = TextureBuffer::BufferInfo(edgeSize, edgeSize, 1, GL_R32F, GL_RED);
    HasBuffer() = true;
    return true;
}


// Erzeuge nahtlose 2D-Noise-Textur (R8). yPeriod/Y sollten dem Tile-Raster entsprechen (z.B. 48,32).
bool NoiseTexture::Create(int edgeSize, int yPeriod, int xPeriod, int octaves, uint32_t seed) {
    if (not Texture::Create())
        return false;
    if (not Allocate(edgeSize))
        return false;
    ComputeNoise(edgeSize, yPeriod, xPeriod, octaves, seed);
    Deploy();
    return true;
}


void NoiseTexture::SetParams(bool enforce) {
    if (enforce || !m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // nahtlos
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}


void NoiseTexture::Deploy(int bufferIndex) {
    if (Bind()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        TextureBuffer* texBuf = m_buffers[0];
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, texBuf->Width(), texBuf->Height(), 0, GL_RED, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        Release();
    }
}

// =================================================================================================

static inline float fade(float t) {
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}


static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}


static inline float hash3i(int x, int y, int z) {
    uint32_t n = (uint32_t)(x) * 73856093u ^ (uint32_t)(y) * 19349663u ^ (uint32_t)(z) * 83492791u;
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;
    return (n & 0xFFFFFFu) / 16777215.0f; // [0..1)
}


static float valueNoise(float x, float y, float z) {
    int xi = int (floorf(x)),
        yi = int (floorf(y)),
        zi = int (floorf(z));
    float xf = x - xi,
        yf = y - yi,
        zf = z - zi;
    float u = fade(xf), v = fade(yf), w = fade(zf);
    float n000 = hash3i(xi, yi, zi);
    float n100 = hash3i(xi + 1, yi, zi);
    float n010 = hash3i(xi, yi + 1, zi);
    float n110 = hash3i(xi + 1, yi + 1, zi);
    float n001 = hash3i(xi, yi, zi + 1);
    float n101 = hash3i(xi + 1, yi, zi + 1);
    float n011 = hash3i(xi, yi + 1, zi + 1);
    float n111 = hash3i(xi + 1, yi + 1, zi + 1);
    float nx00 = lerp(n000, n100, u),
        nx10 = lerp(n010, n110, u);
    float nx01 = lerp(n001, n101, u),
        nx11 = lerp(n011, n111, u);
    float nxy0 = lerp(nx00, nx10, v),
        nxy1 = lerp(nx01, nx11, v);
    return lerp(nxy0, nxy1, w); // [0..1]
}


void NoiseTexture3D::ComputeNoise(void) {
    float* data = reinterpret_cast<float*>(m_data.Data());

    float freq = 2.0f;
    int   oct = 4;
    float amp = 0.5f, gain = 0.5f, lac = 2.0f;

    const float r = 0.70710678f;
    const float warp = 0.15f;

    for (int z = 0; z < m_edgeSize; ++z)
        for (int y = 0; y < m_edgeSize; ++y)
            for (int x = 0; x < m_edgeSize; ++x) {

                float X = (x / (float)m_edgeSize) * freq;
                float Y = (y / (float)m_edgeSize) * freq;
                float Z = (z / (float)m_edgeSize) * freq;

                float Xr = (X + Y) * r;
                float Yr = (Y + Z) * r;
                float Zr = (Z + X) * r;

                float wx = valueNoise(Xr * 2.0f, Yr * 2.0f, Zr * 2.0f) - 0.5f;
                float wy = valueNoise(Xr * 2.0f + 31.0f, Yr * 2.0f, Zr * 2.0f) - 0.5f;
                float wz = valueNoise(Xr * 2.0f, Yr * 2.0f + 19.0f, Zr * 2.0f) - 0.5f;
                Xr += warp * wx; Yr += warp * wy; Zr += warp * wz;

                float s = 0.0f, a = amp, fx = 1.0f;
                for (int o = 0; o < oct; ++o) { s += a * valueNoise(Xr * fx, Yr * fx, Zr * fx); a *= gain; fx *= lac; }

                float norm = 1.0f - powf(gain, (float)oct);
                s = (norm > 0.0f) ? s / norm : s;
                // clamp auf [0..1], direkt als float ablegen
                s = fminf(fmaxf(s, 0.0f), 1.0f);

                data[(z * m_edgeSize + y) * m_edgeSize + x] = s;
            }
}


bool NoiseTexture3D::Allocate(int edgeSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (!texBuf) return false;
    if (!m_buffers.Append(texBuf)) { delete texBuf; return false; }

    m_edgeSize = edgeSize; // falls nicht schon gesetzt
    m_data.Resize(edgeSize * edgeSize * edgeSize * sizeof(float)); // floats

    texBuf->m_info = TextureBuffer::BufferInfo(edgeSize * edgeSize * edgeSize,
        1, 1, GL_R16F, GL_RED); // internes Format = R16F
    HasBuffer() = true;
    return true;
}


// Erzeuge nahtlose 2D-Noise-Textur (R8). yPeriod/Y sollten dem Tile-Raster entsprechen (z.B. 48,32).
bool NoiseTexture3D::Create(int edgeSize) {
    if (not Texture::Create())
        return false;
    if (not Allocate(edgeSize))
        return false;
    ComputeNoise();
    Deploy();
    return true;
}


void NoiseTexture3D::SetParams(bool enforce) {
    if (enforce || !m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}


void NoiseTexture3D::Deploy(int bufferIndex) {
    if (Bind()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // float-Alignment
        TextureBuffer* texBuf = m_buffers[0];
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F,
            m_edgeSize, m_edgeSize, m_edgeSize, 0, GL_RED, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        glGenerateMipmap(GL_TEXTURE_3D);
        Release();
    }
}

// =================================================================================================
