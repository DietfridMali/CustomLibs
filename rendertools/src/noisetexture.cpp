
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "noisetexture.h"

// =================================================================================================

static inline uint32_t pcg_hash(uint32_t x) {
    x ^= 0x9e3779b9u;
    x = x * 747796405u + 2891336453u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
}

static inline float Hash2i(int ix, int iy, uint32_t seed) {
    uint32_t h = pcg_hash((uint32_t)ix * 0x8da6b343u ^ (uint32_t)iy * 0xd8163841u ^ seed);
    return (h >> 8) * (1.0f / 16777215.0f); // ~[0,1)
}

static inline float Smoothe(float t) { 
    return t * t * (3.0f - 2.0f * t); 
}

static float ValueNoisePeriodic(float x, float y, int perX, int perY, uint32_t seed) {
    int ix0 = (int)std::floor(x);
    int iy0 = (int)std::floor(y);
    float fx = x - ix0, fy = y - iy0;
    int ix1 = ix0 + 1, iy1 = iy0 + 1;

    auto wrap = [](int a, int p) { int r = a % p; return r < 0 ? r + p : r; };
    float a = Hash2i(wrap(ix0, perX), wrap(iy0, perY), seed);
    float b = Hash2i(wrap(ix1, perX), wrap(iy0, perY), seed);
    float c = Hash2i(wrap(ix0, perX), wrap(iy1, perY), seed);
    float d = Hash2i(wrap(ix1, perX), wrap(iy1, perY), seed);

    float ux = Smoothe(fx), uy = Smoothe(fy);
    float ab = a + (b - a) * ux;
    float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy;
}

static float fbmPeriodic(float x, float y, int perX, int perY, int octaves = 3, float lacunarity = 2.0f, float gain = 0.5f, uint32_t seed = 1) {
    float s = 0.0f, amp = 1.0f, norm = 0.0f;
    float px = x, py = y;
    int pX = perX, pY = perY; // Perioden skalieren pro Oktave mit
    for (int o = 0; o < octaves; ++o) {
        s += amp * ValueNoisePeriodic(px, py, pX, pY, seed + 101u * o);
        norm += amp;
        px *= lacunarity; py *= lacunarity;
        pX *= (int)lacunarity; 
        pY *= (int)lacunarity; // bleibt periodisch
        amp *= gain;
    }
    s /= std::max(norm, 1e-6f);
    // Kontrast wie im Shader
    s = 0.5f + (s - 0.5f) * 2.2f;
    return std::clamp(s, 0.0f, 1.0f);
}


void NoiseTexture::ComputeNoise(int edgeSize, int yPeriod, int xPeriod, int octaves, uint32_t seed) {
    TextureBuffer* texBuf = m_buffers[0];
    uint8_t* data = texBuf->DataBuffer();
    for (int y = 0; y < edgeSize; ++y) {
        for (int x = 0; x < edgeSize; ++x) {
            // sample-Koords in Periodenraum
            float sx = (x + 0.5f) / edgeSize * yPeriod;
            float sy = (y + 0.5f) / edgeSize * xPeriod;
            float n = fbmPeriodic(sx, sy, yPeriod, xPeriod, octaves, 2.0f, 0.5f, seed);
            data[y * edgeSize + x] = (uint8_t)std::lround(std::clamp(n, 0.0f, 1.0f) * 255.0f);
        }
    }
}

    
bool NoiseTexture::Allocate(int size) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (!texBuf)
        return false;
    if (!m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    if (!texBuf->Allocate(size, 1, 1))
        return false;
    texBuf->m_info = TextureBuffer::BufferInfo(size, 1, 1, GL_R8, GL_RED);
    return true;
}


// Erzeuge nahtlose 2D-Noise-Textur (R8). yPeriod/Y sollten dem Tile-Raster entsprechen (z.B. 48,32).
bool NoiseTexture::Create(int edgeSize, int yPeriod, int xPeriod, int octaves, uint32_t seed) {
    if (not Texture::Create())
        return false;
    if (not Allocate(edgeSize * edgeSize))
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
    if (!IsAvailable())
        return;
    Bind();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    TextureBuffer* texBuf = m_buffers[0];
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texBuf->Width(), texBuf->Height(), 0, GL_RED, GL_UNSIGNED_BYTE, reinterpret_cast<const void*>(texBuf->DataBuffer()));
    SetParams(false);
    Release();
}

// =================================================================================================
