
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>

#include "noisetexture.h"
#include "base_renderer.h"
#include "conversions.hpp"

#include "perlin.h"

#define NORMALIZE_NOISE 0

// =================================================================================================
// Helpers

static inline float Fade(float t) { 
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f); 
}

static inline float Lerp(float a, float b, float t) {
    return a + t * (b - a);
}

uint32_t Hash3D(uint32_t x, uint32_t y, uint32_t z, uint32_t seed) {
    uint32_t v = x * 0x27d4eb2du ^ (y + 0x9e3779b9u); v ^= z * 0x85ebca6bu ^ seed;
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
    return v;
}

static inline float R01(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); } // [0,1)


static float PerlinFBM3_Periodic(float x, float y, float z, int P, int oct, float lac, float gain, uint32_t seed) {
    return 0;
#if 0
    float a = 1.f;
    float sum = 0.f;
    float norm = 0.f;

    for (int i = 0; i < oct; ++i) {
        sum += a * PerlinNoise3D(x, y, z, P, seed + (uint32_t)(i * 131));
        norm += a;
        a *= gain;
        x *= lac;
        y *= lac;
        z *= lac;
    }

    if (norm <= 0.f) 
        return 0.5f;
    float v = sum / norm;        // grob [-1,1]
    v = 0.5f + 0.5f * v;         // -> [0,1]
    return std::clamp(v, 0.0f, 1.0f);
#endif
}

// -------------------------------------------------------------
// Periodischer Worley 3D (F1) + invertiert + fBm
float WorleyF1_Periodic(float X, float Y, float Z, int period, int cells, uint32_t seed) {
    float scale = float(cells) / float(period);
    float x = X * scale;
    float y = Y * scale;
    float z = Z * scale;
    int cx = int(floorf(x));
    int cy = int(floorf(y));
    int cz = int(floorf(z));
    float dmin = 1e9f;
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int ix = Wrap(cx + dx, cells);
                int iy = Wrap(cy + dy, cells);
                int iz = Wrap(cz + dz, cells);
                uint32_t h = Hash3D((uint32_t)ix, (uint32_t)iy, (uint32_t)iz, seed);
                float jx = R01(h);
                float jy = R01(h * 0x9e3779b9u + 0x7f4a7c15u);
                float jz = R01(h * 0x85ebca6bu + 0xc2b2ae35u);
                float fx = float(ix) + jx;
                float fy = float(iy) + jy;
                float fz = float(iz) + jz;
                float dxw = x - fx;
                float dyw = y - fy;
                float dzw = z - fz;
                dxw -= floorf(dxw / float(cells) + 0.5f) * float(cells);
                dyw -= floorf(dyw / float(cells) + 0.5f) * float(cells);
                dzw -= floorf(dzw / float(cells) + 0.5f) * float(cells);
                float d = sqrtf(dxw * dxw + dyw * dyw + dzw * dzw);
                if (d < dmin) dmin = d;
            }
    float invMax = 1.0f / 1.7320508075688772f;
    return std::clamp(dmin * invMax, 0.0f, 1.0f);
}


float WorleyFBM_Periodic(float X, float Y, float Z, int period, int cells0, int oct, float lac, float gain, uint32_t seed) {
    float a = 1.f;
    float sum = 0.f;
    float norm = 0.f;
    int cells = cells0;
    for (int i = 0; i < oct and cells <= period; i++) {
        float v = WorleyF1_Periodic(X, Y, Z, period, cells, seed + uint32_t(i * 733));
        sum += a * v;
        norm += a;
        a *= gain;
        cells = int(cells * lac);
    }
    if (norm <= 0.f) return 0.5f;
    return std::clamp(sum / norm, 0.0f, 1.0f);
}

float WorleyF1_Tiled(float u, float v, float w, int cells, uint32_t seed) {
    u -= floorf(u);
    v -= floorf(v);
    w -= floorf(w);
    float x = u * cells;
    float y = v * cells;
    float z = w * cells;
    int ix = int(floorf(x));
    int iy = int(floorf(y));
    int iz = int(floorf(z));
    float dmin = 1e9f;
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int cx = Wrap(ix + dx, cells);
                int cy = Wrap(iy + dy, cells);
                int cz = Wrap(iz + dz, cells);
                uint32_t h = Hash3D((uint32_t)cx, (uint32_t)cy, (uint32_t)cz, seed);
                float jx = R01(h);
                float jy = R01(h * 0x9e3779b9u + 0x7f4a7c15u);
                float jz = R01(h * 0x85ebca6bu + 0xc2b2ae35u);
                float fx = float(cx) + jx;
                float fy = float(cy) + jy;
                float fz = float(cz) + jz;
                float dxw = x - fx;
                float dyw = y - fy;
                float dzw = z - fz;
                dxw -= roundf(dxw / float(cells)) * float(cells);
                dyw -= roundf(dyw / float(cells)) * float(cells);
                dzw -= roundf(dzw / float(cells)) * float(cells);
                float d = sqrtf(dxw * dxw + dyw * dyw + dzw * dzw);
                if (d < dmin) dmin = d;
            }
    float invMax = 1.0f / 1.7320508075688772f;
    return std::clamp(dmin * invMax, 0.0f, 1.0f);
}


float WorleyFBM_Tiled(float u, float v, float w, int maxCells, int cells0, int oct, float lac, float gain, uint32_t seed) {
    float a = 1.f;
    float sum = 0.f;
    float norm = 0.f;
    int cells = cells0;
    for (int i = 0; i < oct && cells <= maxCells; i++) {
        sum += a * WorleyF1_Tiled(u, v, w, cells, seed + uint32_t(i * 733));
        norm += a;
        a *= gain;
        cells = int(cells * lac);
    }
    if (norm <= 0.f) return 0.5f;
    return std::clamp(sum / norm, 0.0f, 1.0f);
}

// -------------------------------------------------------------
// Optionale 2D-ValueNoise (wie im Original)
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


float fbmPeriodic(float x, float y, int perX, int perY, int octaves, float lac, float gain) {
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


uint8_t Hash2iByte(int ix, int iy, uint32_t seed, uint32_t ch) {
    float phase = float((seed ^ (ch * 0x9E3779B9u)) & 0xFFFFu) * (1.0f / 65536.0f);
    float t = float(ix) * 127.1f + float(iy) * 311.7f + phase;
    float s = std::sin(t) * 43758.5453f;
    float f = s - std::floor(s);
    int   v = int(f * 255.0f + 0.5f);
    return (uint8_t)std::min(v, 255);
}


uint32_t HashXYC32(int x, int y, uint32_t seed, uint32_t ch) {
    uint32_t v = (uint32_t)x;
    v ^= (uint32_t)y * 0x27d4eb2dU;
    v ^= seed * 0x9e3779b9U;
    v ^= (ch + 1U) * 0x85ebca6bU;
    v ^= v >> 16; 
    v *= 0x7feb352dU;
    v ^= v >> 15; 
    v *= 0x846ca68bU;
    v ^= v >> 16; 
    return v;
}

// =================================================================================================
/* für volumetrische Wolken:
    NoiseTexture3D shapeTex3D, detailTex3D;

    Noise3DParams pShape;
    pShape.seed = 0xA5A5A5u;
    pShape.base = 2.032f;   // grobe Strukturen
    pShape.lac = 2.3f;
    pShape.oct = 5;
    pShape.warp = 0.08f;

    Noise3DParams pDetail = pShape;
    pDetail.seed = 0x9e3779b9u;
    pDetail.base = 0.6f;    // höhere Frequenz
    pDetail.lac = 2.8f;
    pDetail.oct = 4;
    pDetail.warp = 0.05f;

    shapeTex3D.Create(128, pShape);   // oder 256, wenn Speicher reicht
    detailTex3D.Create(32,  pDetail);
*/
// --- Simplex periodisch (wie zuletzt bei dir) ---
static const float g3[12][3] = { {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1} };


static void BuildPermutation(std::vector<int>& perm, int period, uint32_t seed) {
    perm.resize(period); 
    for (int i = 0; i < period; ++i) 
        perm[i] = i;
    uint32_t s = seed ? seed : 0x9E3779B9u;
    for (int i = period - 1; i > 0; --i) {
        s = s * 1664525u + 1013904223u;
        int j = int(s % uint32_t(i + 1));
        std::swap(perm[i], perm[j]);
    }
}


static inline int hash3(int ix, int iy, int iz, const std::vector<int>& perm, int period) {
    int a = perm[Wrap(ix, period)], 
        b = perm[(a + Wrap(iy, period)) % period], 
        c = perm[(b + Wrap(iz, period)) % period];
    return c % 12;
}


static inline float dot3(const float g[3], float x, float y, float z) {
    return g[0] * x + g[1] * y + g[2] * z;
}


static float SNoise3Periodic(float x, float y, float z, const std::vector<int>& perm, int period) {
    const float F3 = 1.0f / 3.0f, G3 = 1.0f / 6.0f;
    float s = (x + y + z) * F3; 
    float xs = x + s, ys = y + s, zs = z + s;
    int i = int(floorf(xs)),
        j = int(floorf(ys)), 
        k = int(floorf(zs));
    float t = float(i + j + k) * G3; 
    float X0 = i - t, Y0 = j - t, Z0 = k - t;
    float x0 = x - X0, y0 = y - Y0, z0 = z - Z0;
    int i1 = x0 >= y0 ? 1 : 0, j1 = x0 < y0 ? 1 : 0;
    int i2 = x0 >= z0 ? 1 : 0, k1 = x0 < z0 ? 1 : 0;
    int j2 = y0 >= z0 ? 1 : 0, k2 = y0 < z0 ? 1 : 0;
    i2 &= i1; j2 |= j1;
    float x1 = x0 - i1 + G3, 
          y1 = y0 - j1 + G3, 
          z1 = z0 - k1 + G3;
    float x2 = x0 - i2 + 2.0f * G3, 
          y2 = y0 - j2 + 2.0f * G3, 
          z2 = z0 - (k1 | k2) + 2.0f * G3;
    float x3 = x0 - 1.0f + 3.0f * G3, 
          y3 = y0 - 1.0f + 3.0f * G3, 
          z3 = z0 - 1.0f + 3.0f * G3;
    int gi0 = hash3(i, j, k, perm, period);
    int gi1 = hash3(i + i1, j + j1, k + k1, perm, period);
    int gi2 = hash3(i + i2, j + j2, k + (k1 | k2), perm, period);
    int gi3 = hash3(i + 1, j + 1, k + 1, perm, period);
    float n0 = 0, n1 = 0, n2 = 0, n3 = 0;
    float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0; 
    if (t0 > 0) { 
        t0 *= t0; 
        n0 = t0 * t0 * dot3(g3[gi0], x0, y0, z0); 
    }
    float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1; 
    if (t1 > 0) { 
        t1 *= t1; 
        n1 = t1 * t1 * dot3(g3[gi1], x1, y1, z1); 
    }
    float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2; 
    if (t2 > 0) { 
        t2 *= t2; 
        n2 = t2 * t2 * dot3(g3[gi2], x2, y2, z2); 
    }
    float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3; 
    if (t3 > 0) { 
        t3 *= t3; 
        n3 = t3 * t3 * dot3(g3[gi3], x3, y3, z3); 
    }
    return 32.0f * (n0 + n1 + n2 + n3); // ~[-1,1]
}

// -------------------------------------------------------------------------------------------------

// 3D Simplex Noise (Ashima/Gustavson), Output ca. [-1,1]

static inline float dotGrad3(int g, float x, float y, float z) {
    static const int grad3[12][3] = {
        { 1, 1, 0},{-1, 1, 0},{ 1,-1, 0},{-1,-1, 0},
        { 1, 0, 1},{-1, 0, 1},{ 1, 0,-1},{-1, 0,-1},
        { 0, 1, 1},{ 0,-1, 1},{ 0, 1,-1},{ 0,-1,-1}
    };

    return grad3[g][0] * x + grad3[g][1] * y + grad3[g][2] * z;
}

static float SNoise3Ashima(float x, float y, float z) {
    const float F3 = 1.0f / 3.0f;
    const float G3 = 1.0f / 6.0f;

    float s = (x + y + z) * F3;
    int i = (int)floorf(x + s);
    int j = (int)floorf(y + s);
    int k = (int)floorf(z + s);

    float t = (i + j + k) * G3;
    float X0 = (float)i - t;
    float Y0 = (float)j - t;
    float Z0 = (float)k - t;

    float x0 = x - X0;
    float y0 = y - Y0;
    float z0 = z - Z0;

    int i1, j1, k1;
    int i2, j2, k2;

    if (x0 >= y0) {
        if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
        else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
    }
    else {
        if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
        else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
        else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    }

    float x1 = x0 - (float)i1 + G3;
    float y1 = y0 - (float)j1 + G3;
    float z1 = z0 - (float)k1 + G3;
    float x2 = x0 - (float)i2 + 2.0f * G3;
    float y2 = y0 - (float)j2 + 2.0f * G3;
    float z2 = z0 - (float)k2 + 2.0f * G3;
    float x3 = x0 - 1.0f + 3.0f * G3;
    float y3 = y0 - 1.0f + 3.0f * G3;
    float z3 = z0 - 1.0f + 3.0f * G3;

    static const int p[256] = {
        151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
        140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
        247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
        57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
        74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
        60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
        65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
        200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
        52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
        207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
        119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
        129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
        218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
        81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
        184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
        222,114,67,29,24,72,243,141,128,195,78,66,215
    };

    static int perm[512];
    static int permMod12[512];
    static bool init = false;
    if (!init) {
        for (int n = 0; n < 512; ++n) {
            perm[n] = p[n & 255];
            permMod12[n] = perm[n] % 12;
        }
        init = true;
    }

    int ii = i & 255;
    int jj = j & 255;
    int kk = k & 255;

    int gi0 = permMod12[ii + perm[jj + perm[kk]]];
    int gi1 = permMod12[ii + i1 + perm[jj + j1 + perm[kk + k1]]];
    int gi2 = permMod12[ii + i2 + perm[jj + j2 + perm[kk + k2]]];
    int gi3 = permMod12[ii + 1 + perm[jj + 1 + perm[kk + 1]]];

    float n0, n1, n2, n3;

    float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
    if (t0 < 0.0f) n0 = 0.0f;
    else { t0 *= t0; n0 = t0 * t0 * dotGrad3(gi0, x0, y0, z0); }

    float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
    if (t1 < 0.0f) n1 = 0.0f;
    else { t1 *= t1; n1 = t1 * t1 * dotGrad3(gi1, x1, y1, z1); }

    float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
    if (t2 < 0.0f) n2 = 0.0f;
    else { t2 *= t2; n2 = t2 * t2 * dotGrad3(gi2, x2, y2, z2); }

    float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
    if (t3 < 0.0f) n3 = 0.0f;
    else { t3 *= t3; n3 = t3 * t3 * dotGrad3(gi3, x3, y3, z3); }

    return 32.0f * (n0 + n1 + n2 + n3);
}

// -------------------------------------------------------------------------------------------------

bool NoiseTexture3D::Allocate(int gridSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf) 
        return false;
    if (not m_buffers.Append(texBuf)) { 
        delete texBuf; 
        return false; 
    }
    m_gridSize = gridSize;
    m_data.Resize(gridSize * gridSize * gridSize * 4);
    texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize, 1, GL_R16F, GL_RED);
    HasBuffer() = true;
    return true;
}


bool NoiseTexture3D::Create(int gridSize, const Noise3DParams& params, String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = GL_TEXTURE_3D;
    if (not Allocate(gridSize))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        ComputeNoise();
        SaveToFile(noiseFilename);
    }
    Deploy();
    return true;
}


void NoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.Data();

    const float cellSize = float(m_gridSize) / float(m_params.cellsPerAxis); // Anzahl Perlin-Zellen pro Kachel

#if 1

    struct PerlinFunctor {
        float operator()(float x, float y, float z) const {
            return Perlin::Noise(x, y, z); // ~[-1,1]
        }
    };

    PerlinFunctor perlinFn{};
    FBM<PerlinFunctor> fbm(perlinFn, m_params.fbmParams);

    Vector4f noise;
    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ 0.0f, 0.0f, 0.0f, 0.0f };
    int belowThreshold = 0;

    int i = 0;
    for (int z = 0; z < m_gridSize; ++z) {
        float w = float(z) / cellSize;
        for (int y = 0; y < m_gridSize; ++y) {
            float v = float(y) / cellSize;
            for (int x = 0; x < m_gridSize; ++x) {
                float u = float(x) / cellSize;
#if 0
                noise.x = (1.0f + Perlin::Noise(u, v, w)) * 0.5f; // fbm.Value(u, v, w); // [0,1]
#else
                noise.x = fbm.Value(u, v, w);
#endif
                minVals.Minimize(noise);
                maxVals.Maximize(noise);

                data[i++] = std::clamp(noise.x, 0.0f, 1.0f);
                if (noise.x < 0.3125)
                    ++belowThreshold;
                data[i++] = 0.0f; // std::clamp(noise.y, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.z, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.w, 0.0f, 1.0f);
            }
        }
    }

#else

    struct PerlinFunctor {
        const std::vector<int>& perm;
        int period;
        float operator()(float x, float y, float z) const {
            return Perlin::ImprovedNoise(x, y, z, perm, m_params.cellsPerAxis); // ~[-1,1]
        }
    };

    std::vector<int> perm;
    Perlin::BuildPermutation(perm, m_params.cellsPerAxis, m_params.seed);
    PerlinFunctor perlinFn{ perm, m_params.cellsPerAxis };
    FBM<PerlinFunctor> fbm(perlinFn, m_params.fbmParams);

    Vector4f noise;
    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ 0.0f, 0.0f, 0.0f, 0.0f };

    int i = 0;
    for (int z = 0; z < m_gridSize; ++z) {
        float w = float(z) / float(m_gridSize) * m_params.cellsPerAxis;
        for (int y = 0; y < m_gridSize; ++y) {
            float v = float(y) / float(m_gridSize) * m_params.cellsPerAxis;
            for (int x = 0; x < m_gridSize; ++x) {
                float u = float(x) / float(m_gridSize) * m_params.cellsPerAxis;

                noise.x = fbm.Value(u, v, w); // [0,1]
                minVals.Minimize(noise);
                maxVals.Maximize(noise);

                data[i++] = std::clamp(noise.x, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.y, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.z, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.w, 0.0f, 1.0f);
            }
        }
    }

#endif

#   if 0
    for (int z = 0; z < m_gridSize; ++z) {
        for (int y = 0; y < m_gridSize; ++y) {
            for (int x = 0; x < m_gridSize; ++x) {
                noise.x = PerlinNoise(float(x) / cellSize, float(y) / cellSize, float(z) / cellSize);
                // Perlin-fBm für Basis
                float perlin = 0.0f;
                float a = m_params.initialGain;
                float f = m_params.baseFrequency * float(perlinPeriod);

                for (int o = 0; o < m_params.octaves; ++o) {
                    //float n = PerlinNoise3D(X * f, Y * f, Z * f, 8, m_params.seed ^ 0xA5A5A5u); // ~[-1,1]
                    float n = PerlinNoise(X * f, Y * f, Z * f, perm, perlinPeriod);
                    perlin += a * n;
                    a *= m_params.gain;
                    f *= m_params.lacunarity;
                }

                perlin = 0.5f + 0.5f * (perlin / perlinNorm);
                perlin = std::clamp(perlin, 0.0f, 1.0f);

                // Worley-fBm für Kanäle
                //noise.x = WorleyF1_Periodic(X, Y, Z, m_gridSize, C_R, m_params.seed ^ 0x51u);
                noise.x = perlin; // *(1.0f - noise.x);
#   if 0
                noise.x = std::pow(noise.x, 0.5f);
                noise.x = std::clamp(noise.x, 0.0f, 1.0f);
#   endif
#   if 0
#       if 1
                noise.y = WorleyFBM_Periodic(X, Y, Z, m_gridSize, C_G, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0x1337u);
                noise.z = WorleyFBM_Periodic(X, Y, Z, m_gridSize, C_B, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xBEEFu);
                noise.w = WorleyFBM_Periodic(X, Y, Z, m_gridSize, C_A, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xCAFEu);
#      else
                noise.y = WorleyFBM_Tiled(X, Y, Z, m_gridSize, C_G, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0x1337u);
                noise.z = WorleyFBM_Tiled(X, Y, Z, m_gridSize, C_B, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xBEEFu);
                noise.w = WorleyFBM_Tiled(X, Y, Z, m_gridSize, C_A, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xCAFEu);
#       endif
#   endif
                minVals.Minimize(noise);
                maxVals.Maximize(noise);

                data[idx++] = std::clamp(noise.x, 0.0f, 1.0f);
                data[idx++] = 0.0f; // std::clamp(noise.y, 0.0f, 1.0f);
                data[idx++] = 0.0f; // std::clamp(noise.z, 0.0f, 1.0f);
                data[idx++] = 0.0f; // std::clamp(noise.w, 0.0f, 1.0f);
            }
        }
    }
#endif

#if 1
    if (m_params.normalize) {
        int h = i;
        for (int i = 0; i < h; ) {
            if (m_params.normalize & 1)
                Conversions::Normalize(data[i], minVals.x, maxVals.x);
            ++i;
            if (m_params.normalize & 2)
                Conversions::Normalize(data[i++], minVals.y, maxVals.y);
            ++i;
            if (m_params.normalize & 4)
                Conversions::Normalize(data[i++], minVals.z, maxVals.z);
            ++i;
            if (m_params.normalize & 8)
                Conversions::Normalize(data[i++], minVals.w, maxVals.w);
            ++i;
        }
    }
#endif
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
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, m_gridSize, m_gridSize, m_gridSize, 0, GL_RGBA, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data())
        );
        SetParams(false);
        glGenerateMipmap(GL_TEXTURE_3D);
        Release();
    }
}

bool NoiseTexture3D::LoadFromFile(const String& filename) {
    if (filename.IsEmpty())
        return false;
    std::ifstream f((const char*)filename, std::ios::binary);
    if (not f)
        return false;

    int voxelCount = size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize) * 4u;
    int expectedBytes = voxelCount * sizeof(float);

    f.seekg(0, std::ios::end);
    std::streamoff fileSize = f.tellg();
    f.seekg(0, std::ios::beg);

    if (fileSize != std::streamoff(expectedBytes))
        return false;

    if (m_data.Length() != voxelCount)
        m_data.Resize(voxelCount);

    f.read(reinterpret_cast<char*>(m_data.Data()), expectedBytes);
    return f.good();
}


bool NoiseTexture3D::SaveToFile(const String& filename) const {
    if (filename.IsEmpty())
        return false;
    std::ofstream f((const char*)filename, std::ios::binary | std::ios::trunc);
    if (not f)
        return false;

    size_t voxelCount = size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize) * 4u;
    size_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.Data()), bytes);
    return f.good();
}

// =================================================================================================

bool CloudVolume3D::Allocate(int gridSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_gridSize = gridSize;
    m_data.Resize(size_t(gridSize) * gridSize * gridSize);
    texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize, 1, GL_R16F, GL_RED);
    HasBuffer() = true;
    return true;
}


bool CloudVolume3D::Create(int gridSize, const CloudVolumeParams& params, String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = GL_TEXTURE_3D;
    if (not Allocate(gridSize))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        Compute();
        SaveToFile(noiseFilename);
    }
    Deploy();
    return true;
}

#if 0

void CloudVolume3D::Compute() {
    float* data = m_data.Data();
    size_t idx = 0;

    for (int z = 0; z < m_gridSize; ++z) {
        for (int y = 0; y < m_gridSize; ++y) {
            for (int x = 0; x < m_gridSize; ++x) {
                data[idx++] = PerlinFBM3_Periodic(float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f, m_gridSize, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed);
            }
        }
    }
}

#else

void CloudVolume3D::Compute(void) {
    float* data = m_data.Data();

    std::vector<int> perm;
    BuildPermutation(perm, m_gridSize, m_params.seed ? m_params.seed : 0x9E3779B9u);

    float norm = 0.0f;
    float a = m_params.initialGain;
    for (int o = 0; o < m_params.octaves; ++o) {
        norm += a;
        a *= m_params.gain;
    }
    if (norm <= 0.0f)
        norm = 1.0f;

    size_t idx = 0;
#if NORMALIZE_NOISE
    float maxVal = 0.0f;
#endif
    for (int z = 0; z < m_gridSize; ++z) {
        float w = (float(z) + 0.5f) / float(m_gridSize);
        for (int y = 0; y < m_gridSize; ++y) {
            float v = (float(y) + 0.5f) / float(m_gridSize);
            for (int x = 0; x < m_gridSize; ++x) {
                float u = (float(x) + 0.5f) / float(m_gridSize);
                // in periodischen Raum [0,m_gridSize)

                float t = 0.0f;
                float a = m_params.initialGain;
                float f = float(m_gridSize);

                for (int o = 0; o < m_params.octaves; ++o) {
                    float n = SNoise3Periodic(u * f, v * f, w * f, perm, m_gridSize); // ~[-1,1], m_gridSize-periodisch
                    t += fabs(n) * a;
                    a *= m_params.gain;
                    f *= m_params.lacunarity;
                }

                //t = (1.0f + t) * 0.5f;
                float d = t / norm;              // ~0..1
                d = std::clamp(d, 0.0f, 1.0f);
                data[idx++] = d;
#if NORMALIZE_NOISE
                if (maxVal < d)
                    maxVal = d;
#endif
            }
        }
    }

#if NORMALIZE_NOISE
    if ((maxVal > 0.0f) and (maxVal < 0.999f)) {
        for (; idx; --idx, data++)
            *data /= maxVal;
    }
#endif
}

#endif

void CloudVolume3D::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}


void CloudVolume3D::Deploy(int) {
    if (Bind()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, m_gridSize, m_gridSize, m_gridSize, 0, GL_RED, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        glGenerateMipmap(GL_TEXTURE_3D);
        Release();
    }
}


bool CloudVolume3D::LoadFromFile(const String& filename) {
    if (filename.IsEmpty())
        return false;
    std::ifstream f((const char*)filename, std::ios::binary);
    if (not f)
        return false;

    int voxelCount = size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize);
    int expectedBytes = voxelCount * sizeof(float);

    f.seekg(0, std::ios::end);
    std::streamoff fileSize = f.tellg();
    f.seekg(0, std::ios::beg);

    if (fileSize != std::streamoff(expectedBytes))
        return false;

    if (m_data.Length() != voxelCount)
        m_data.Resize(voxelCount);

    f.read(reinterpret_cast<char*>(m_data.Data()), expectedBytes);
    return f.good();
}


bool CloudVolume3D::SaveToFile(const String& filename) const {
    if (filename.IsEmpty())
        return false;
    std::ofstream f((const char*)filename, std::ios::binary | std::ios::trunc);
    if (not f)
        return false;

    size_t voxelCount = size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize);
    size_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.Data()), bytes);
    return f.good();
}

// =================================================================================================
