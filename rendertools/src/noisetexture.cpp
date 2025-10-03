
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
