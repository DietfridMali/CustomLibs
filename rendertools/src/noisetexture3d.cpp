
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "noisetexture.h"
#include "base_renderer.h"

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
    int xi = floorf(x), 
        yi = floorf(y), 
        zi = floorf(z);
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
    uint8_t* data = m_data.Data();

    // fBm-Parameter
    float freq = 2.0f;      // Grundfrequenz im Gitter
    int   oct = 4;
    float amp = 0.5f, gain = 0.5f, lac = 2.0f;

    for (int z = 0; z < m_edgeSize; ++z)
        for (int y = 0; y < m_edgeSize; ++y)
            for (int x = 0; x < m_edgeSize; ++x) {
                float X = (x / (float)m_edgeSize) * freq, Y = (y / (float)m_edgeSize) * freq, Z = (z / (float)m_edgeSize) * freq;
                float s = 0.0f, a = amp, fx = 1.0f;
                for (int o = 0; o < oct; ++o) {
                    s += a * valueNoise(X * fx, Y * fx, Z * fx);
                    a *= gain; fx *= lac;
                }
                // Normalisieren grob auf [0..1]
                s = s * (1.0f / (1.0f - std::pow(gain, oct))) * (1.0f - 0.0f);
                s = fminf(fmaxf(s, 0.0f), 1.0f);
                data[(z * m_edgeSize + y) * m_edgeSize + x] = (uint8_t)lrintf(s * 255.0f);
            }
}

    
bool NoiseTexture3D::Allocate(int edgeSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (!texBuf)
        return false;
    if (!m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_data.Resize(edgeSize * edgeSize * edgeSize);
    texBuf->m_info = TextureBuffer::BufferInfo(edgeSize * edgeSize * edgeSize, 1, 1, GL_R32F, GL_RED);
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
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}


void NoiseTexture3D::Deploy(int bufferIndex) {
    if (Bind()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        TextureBuffer* texBuf = m_buffers[0];
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, m_edgeSize, m_edgeSize, m_edgeSize, 0, GL_RED, GL_UNSIGNED_BYTE, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        Release();
    }
}

// =================================================================================================
