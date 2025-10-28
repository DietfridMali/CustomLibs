
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "noisetexture.h"
#include "base_renderer.h"

// =================================================================================================

#if 0 // replaced with template class

static inline float Hash2i(int ix, int iy) {
    // GLSL-kompatibel: fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453)
    // seed wirkt nur als kleine Phasenverschiebung
    float t = float(ix) * 127.1f + float(iy) * 311.7f;
    float s = std::sin(t) * 43758.5453f;
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

#endif 

// =================================================================================================

// --- Simplex periodisch (wie zuletzt bei dir) ---
static inline int wrapi(int a, int p) { 
    int r = a % p; return r < 0 ? r + p : r; 
}

static const float g3[12][3] = { {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1} };


static void buildPermutation(std::vector<int>& perm, int period, uint32_t seed) {
    perm.resize(period); for (int i = 0; i < period; ++i) perm[i] = i;
    uint32_t s = seed ? seed : 0x9E3779B9u;
    for (int i = period - 1; i > 0; --i) { 
        s = s * 1664525u + 1013904223u; 
        int j = int(s % uint32_t(i + 1)); 
        std::swap(perm[i], perm[j]); 
    }
}


static inline int hash3(int ix, int iy, int iz, const std::vector<int>& perm, int period) {
    int a = perm[wrapi(ix, period)], b = perm[(a + wrapi(iy, period)) % period], c = perm[(b + wrapi(iz, period)) % period]; 
    return c % 12;
}


static inline float dot3(const float g[3], float x, float y, float z) { 
    return g[0] * x + g[1] * y + g[2] * z; 
}


static float snoise3_periodic(float x, float y, float z, const std::vector<int>& perm, int period) {
    const float F3 = 1.0f / 3.0f, G3 = 1.0f / 6.0f;
    float s = (x + y + z) * F3; float xs = x + s, ys = y + s, zs = z + s;
    int i = int(floorf(xs)), j = int(floorf(ys)), k = int(floorf(zs));
    float t = float(i + j + k) * G3; float X0 = i - t, Y0 = j - t, Z0 = k - t;
    float x0 = x - X0, y0 = y - Y0, z0 = z - Z0;
    int i1 = x0 >= y0 ? 1 : 0, j1 = x0 < y0 ? 1 : 0;
    int i2 = x0 >= z0 ? 1 : 0, k1 = x0 < z0 ? 1 : 0;
    int j2 = y0 >= z0 ? 1 : 0, k2 = y0 < z0 ? 1 : 0;
    i2 &= i1; j2 |= j1;
    float x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
    float x2 = x0 - i2 + 2.0f * G3, y2 = y0 - j2 + 2.0f * G3, z2 = z0 - (k1 | k2) + 2.0f * G3;
    float x3 = x0 - 1.0f + 3.0f * G3, y3 = y0 - 1.0f + 3.0f * G3, z3 = z0 - 1.0f + 3.0f * G3;
    int gi0 = hash3(i, j, k, perm, period);
    int gi1 = hash3(i + i1, j + j1, k + k1, perm, period);
    int gi2 = hash3(i + i2, j + j2, k + (k1 | k2), perm, period);
    int gi3 = hash3(i + 1, j + 1, k + 1, perm, period);
    float n0 = 0, n1 = 0, n2 = 0, n3 = 0;
    float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0; if (t0 > 0) { t0 *= t0; n0 = t0 * t0 * dot3(g3[gi0], x0, y0, z0); }
    float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1; if (t1 > 0) { t1 *= t1; n1 = t1 * t1 * dot3(g3[gi1], x1, y1, z1); }
    float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2; if (t2 > 0) { t2 *= t2; n2 = t2 * t2 * dot3(g3[gi2], x2, y2, z2); }
    float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3; if (t3 > 0) { t3 *= t3; n3 = t3 * t3 * dot3(g3[gi3], x3, y3, z3); }
    return 32.0f * (n0 + n1 + n2 + n3); // ~[-1,1]
}


// --- Deine Klasse ---
bool NoiseTexture3D::Allocate(int edgeSize) {
    TextureBuffer* texBuf = new TextureBuffer(); if (not texBuf) return false;
    if (not m_buffers.Append(texBuf)) { delete texBuf; return false; }
    m_edgeSize = edgeSize;
    m_data.Resize(size_t(edgeSize) * edgeSize * edgeSize); // floats, 1 Kanal
    texBuf->m_info = TextureBuffer::BufferInfo(size_t(edgeSize) * edgeSize * edgeSize * sizeof(float), 1, 1, GL_R16F, GL_RED);
    HasBuffer() = true; return true;
}


void NoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.Data();
    std::vector<int> perm; 
    buildPermutation(perm, m_edgeSize, 0x1234567u);

    // fBm-Parameter identisch zum Shader
    const float base = 2.032f;
    const float lac = 2.6434f;
    const int   oct = 5;
    const float init_gain = 0.5f;
    const float gain = 0.5f;

    const float rotc = 0.98078528f; // cos(11.25°)
    const float rots = 0.19509032f; // sin(11.25°)
    const float warp = 0.10f;

    const float scaleBase = float(m_edgeSize) / base;
    const float norm = init_gain * (1.0f - powf(gain, (float)oct)) / (1.0f - gain);

    size_t idx = 0;
    for (int z = 0; z < m_edgeSize; ++z)
        for (int y = 0; y < m_edgeSize; ++y)
            for (int x = 0; x < m_edgeSize; ++x) {
                float X = x / scaleBase, Y = y / scaleBase, Z = z / scaleBase;
                // leichte Rotation
                float Xr = rotc * X - rots * Z, Yr = Y, Zr = rots * X + rotc * Z;
                // sehr schwaches Warp
                float wx = snoise3_periodic(Xr * 2.31f, Yr * 2.31f, Zr * 2.31f, perm, m_edgeSize);
                float wy = snoise3_periodic(Xr * 2.31f + 17.0f, Yr * 2.31f, Zr * 2.31f, perm, m_edgeSize);
                float wz = snoise3_periodic(Xr * 2.31f, Yr * 2.31f + 11.0f, Zr * 2.31f, perm, m_edgeSize);
                Xr += warp * wx; Yr += warp * wy; Zr += warp * wz;

                float s = 0.0f, a = init_gain, f = 1.0f;
                for (int o = 0; o < oct; ++o) { s += a * fabsf(snoise3_periodic(Xr * f, Yr * f, Zr * f, perm, m_edgeSize)); a *= gain; f *= lac; }
                if (norm > 0.0f) s /= norm;

                // Kontrast/Offset auf [0..1]
                s = (s - 0.28f) * 1.9f; // an dein Vorbild angepasst
                if (s < 0.0f) s = 0.0f; if (s > 1.0f) s = 1.0f;
                data[idx++] = s;
            }
}


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
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}


void NoiseTexture3D::Deploy(int) {
    if (Bind()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, m_edgeSize, m_edgeSize, m_edgeSize, 0, GL_RED, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        glGenerateMipmap(GL_TEXTURE_3D);
        Release();
    }
}

// =================================================================================================
