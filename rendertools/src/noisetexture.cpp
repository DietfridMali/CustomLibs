
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>

#include "noisetexture.h"
#include "base_renderer.h"

#define NORMALIZE_NOISE 0

// =================================================================================================
// Helpers

static inline float Fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

static inline int  Wrap(int a, int p) { int r = a % p; return r < 0 ? r + p : r; }

uint32_t Hash3D(uint32_t x, uint32_t y, uint32_t z, uint32_t seed) {
    uint32_t v = x * 0x27d4eb2du ^ (y + 0x9e3779b9u); v ^= z * 0x85ebca6bu ^ seed;
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
    return v;
}

static inline float R01(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); } // [0,1)

// -------------------------------------------------------------
// Periodischer Perlin 3D + fBm
static inline void Grad3(uint32_t h, float& gx, float& gy, float& gz) {
    switch (h % 12u) {
    case 0: 
        gx = 1; gy = 1; gz = 0; 
        break; 
    case 1: 
        gx = -1; gy = 1; gz = 0; 
        break;
    case 2: 
        gx = 1; gy = -1; gz = 0; 
        break; 
    case 3: 
        gx = -1; gy = -1; gz = 0; 
        break;
    case 4: 
        gx = 1; gy = 0; gz = 1; 
        break; 
    case 5: 
        gx = -1; gy = 0; gz = 1; 
        break;
    case 6: 
        gx = 1; gy = 0; gz = -1; 
        break; 
    case 7: 
        gx = -1; gy = 0; gz = -1; 
        break;
    case 8: 
        gx = 0; gy = 1; gz = 1; 
        break; 
    case 9: 
        gx = 0; gy = -1; gz = 1; 
        break;
    case 10: 
        gx = 0; gy = 1; gz = -1; 
        break; 
    default: 
        gx = 0; gy = -1; gz = -1; 
        break;
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
float WorleyF1_Periodic(float x, float y, float z, int C, uint32_t seed) {
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
static inline int wrapi(int a, int p) {
    int r = a % p; return r < 0 ? r + p : r;
}

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
    int a = perm[wrapi(ix, period)], 
        b = perm[(a + wrapi(iy, period)) % period], 
        c = perm[(b + wrapi(iz, period)) % period];
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

static float fastFloor(float x) { return x >= 0.0f ? (float)(int)x : (float)((int)x - 1); }

static float SNoise3Ashima(const float x, const float y, const float z)
{
    const float F3 = 1.0f / 3.0f;
    const float G3 = 1.0f / 6.0f;

    const float s = (x + y + z) * F3;
    const float xs = x + s;
    const float ys = y + s;
    const float zs = z + s;

    const int i = (int)fastFloor(xs);
    const int j = (int)fastFloor(ys);
    const int k = (int)fastFloor(zs);

    const float t = (float)(i + j + k) * G3;
    const float X0 = (float)i - t;
    const float Y0 = (float)j - t;
    const float Z0 = (float)k - t;

    const float x0 = x - X0;
    const float y0 = y - Y0;
    const float z0 = z - Z0;

    int i1 = (x0 >= y0) ? 1 : 0;
    int j1 = (x0 < y0) ? 1 : 0;
    int i2 = (x0 >= z0) ? 1 : 0;
    int k1 = (x0 < z0) ? 1 : 0;
    int j2 = (y0 >= z0) ? 1 : 0;
    int k2 = (y0 < z0) ? 1 : 0;
    i2 &= i1; j2 |= j1;

    const float x1 = x0 - (float)i1 + G3;
    const float y1 = y0 - (float)j1 + G3;
    const float z1 = z0 - (float)k1 + G3;
    const float x2 = x0 - (float)i2 + 2.0f * G3;
    const float y2 = y0 - (float)j2 + 2.0f * G3;
    const float z2 = z0 - (float)(k1 | k2) + 2.0f * G3;
    const float x3 = x0 - 1.0f + 3.0f * G3;
    const float y3 = y0 - 1.0f + 3.0f * G3;
    const float z3 = z0 - 1.0f + 3.0f * G3;

    auto mod289 = [](float x) { return x - floorf(x * (1.0f / 289.0f)) * 289.0f; };
    auto mod289v4 = [&](const float v[4], float out[4]) {
        for (int n = 0; n < 4; ++n)
            out[n] = mod289(v[n]);
        };
    auto permute = [&](float x) { return mod289((x * 34.0f + 1.0f) * x); };

    float ii = (float)i;
    float jj = (float)j;
    float kk = (float)k;

    float iVec[4] = { 0.0f, (float)i1, (float)i2, 1.0f };
    float jVec[4] = { 0.0f, (float)j1, (float)j2, 1.0f };
    float kVec[4] = { 0.0f, (float)k1, (float)(k1 | k2), 1.0f };

    float p[4];
    for (int n = 0; n < 4; ++n)
        p[n] = kk + kVec[n];
    for (int n = 0; n < 4; ++n)
        p[n] = permute(p[n] + jj + jVec[n]);
    for (int n = 0; n < 4; ++n)
        p[n] = permute(p[n] + ii + iVec[n]);

    const float n_ = 0.142857142857f;
    const float D1 = 0.0f, D2 = 0.5f, D3 = 1.0f, D4 = 2.0f;
    const float nsx = n_ * D4 - D1;      // wie im Shader (kompakt)
    const float nsy = n_ * D3 - D1;
    const float nsz = n_ * D2 - D1;

    float x_[4], y_[4];
    for (int n = 0; n < 4; ++n) {
        float jn = p[n] - 49.0f * floorf(p[n] * nsz * nsz);
        x_[n] = floorf(jn * nsz);
        y_[n] = floorf(jn - 7.0f * x_[n]);
    }

    float xw[4], yw[4], h[4];
    for (int n = 0; n < 4; ++n) {
        xw[n] = x_[n] * nsx + nsy;
        yw[n] = y_[n] * nsx + nsy;
        h[n] = 1.0f - fabsf(xw[n]) - fabsf(yw[n]);
    }

    float b0x = xw[0], b0y = xw[1];
    float b0z = yw[0], b0w = yw[1];
    float b1x = xw[2], b1y = xw[3];
    float b1z = yw[2], b1w = yw[3];

    float s0x = floorf(b0x) * 2.0f + 1.0f;
    float s0y = floorf(b0y) * 2.0f + 1.0f;
    float s0z = floorf(b0z) * 2.0f + 1.0f;
    float s0w = floorf(b0w) * 2.0f + 1.0f;
    float s1x = floorf(b1x) * 2.0f + 1.0f;
    float s1y = floorf(b1y) * 2.0f + 1.0f;
    float s1z = floorf(b1z) * 2.0f + 1.0f;
    float s1w = floorf(b1w) * 2.0f + 1.0f;

    float sh[4] = {
        -(h[0] < 0.0f ? 1.0f : 0.0f),
        -(h[1] < 0.0f ? 1.0f : 0.0f),
        -(h[2] < 0.0f ? 1.0f : 0.0f),
        -(h[3] < 0.0f ? 1.0f : 0.0f)
    };

    float a0x = b0x + s0x * sh[0];
    float a0y = b0z + s0z * sh[0];
    float a0z = h[0];
    float a1x = b0y + s0y * sh[1];
    float a1y = b0w + s0w * sh[1];
    float a1z = h[1];
    float a2x = b1x + s1x * sh[2];
    float a2y = b1z + s1z * sh[2];
    float a2z = h[2];
    float a3x = b1y + s1y * sh[3];
    float a3y = b1w + s1w * sh[3];
    float a3z = h[3];

    auto invSqrt = [](float r) { return 1.79284291400159f - 0.85373472095314f * r; };

    float norm0 = invSqrt(a0x * a0x + a0y * a0y + a0z * a0z);
    float norm1 = invSqrt(a1x * a1x + a1y * a1y + a1z * a1z);
    float norm2 = invSqrt(a2x * a2x + a2y * a2y + a2z * a2z);
    float norm3 = invSqrt(a3x * a3x + a3y * a3y + a3z * a3z);

    a0x *= norm0; a0y *= norm0; a0z *= norm0;
    a1x *= norm1; a1y *= norm1; a1z *= norm1;
    a2x *= norm2; a2y *= norm2; a2z *= norm2;
    a3x *= norm3; a3y *= norm3; a3z *= norm3;

    float m0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
    float m1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
    float m2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
    float m3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
    if (m0 < 0) m0 = 0; m0 *= m0;
    if (m1 < 0) m1 = 0; m1 *= m1;
    if (m2 < 0) m2 = 0; m2 *= m2;
    if (m3 < 0) m3 = 0; m3 *= m3;

    float n0 = m0 * m0 * (a0x * x0 + a0y * y0 + a0z * z0);
    float n1 = m1 * m1 * (a1x * x1 + a1y * y1 + a1z * z1);
    float n2 = m2 * m2 * (a2x * x2 + a2y * y2 + a2z * z2);
    float n3 = m3 * m3 * (a3x * x3 + a3y * y3 + a3z * z3);

    return 42.0f * (n0 + n1 + n2 + n3);
}

// -------------------------------------------------------------------------------------------------

// Allocate bleibt wie bei dir, aber BufferInfo sauber als 3D Platzhalter:
bool NoiseTexture3D::Allocate(int edgeSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf) 
        return false;
    if (not m_buffers.Append(texBuf)) { 
        delete texBuf; 
        return false; 
    }
    m_edgeSize = edgeSize;
    m_data.Resize(size_t(edgeSize) * size_t(edgeSize) * size_t(edgeSize) * 4u);
    // Werte sind hier irrelevant, dienen nur dem internen Status
    texBuf->m_info = TextureBuffer::BufferInfo(edgeSize, edgeSize, 1, GL_R16F, GL_RED);
    HasBuffer() = true;
    return true;
}


bool NoiseTexture3D::Create(int edgeSize, const Noise3DParams& params, String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = GL_TEXTURE_3D;
    if (not Allocate(edgeSize))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        ComputeNoise();
        SaveToFile(noiseFilename);
    }
    Deploy();
    return true;
}


// ComputeNoise parametrisierbar gemacht (deine Simplex-FBM + Warp)
void NoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.Data();

    const int C0 = m_edgeSize / 8;
    const int C_G = C0;
    const int C_B = C0 * 2;
    const int C_A = C0 * 4;
    const int C_R = C0 * 2;

    float perlinNorm = 0.0f;
    {
        float a = m_params.initialGain;
        for (int o = 0; o < m_params.octaves; ++o) {
            perlinNorm += a;
            a *= m_params.gain;
        }
        if (perlinNorm <= 0.0f)
            perlinNorm = 1.0f;
    }

    size_t idx = 0;

    for (int z = 0; z < m_edgeSize; ++z)
        for (int y = 0; y < m_edgeSize; ++y)
            for (int x = 0; x < m_edgeSize; ++x) {
                float X = float(x) + 0.5f;
                float Y = float(y) + 0.5f;
                float Z = float(z) + 0.5f;

                // Perlin-fBm für Basis
                float ps = 0.0f;
                float a = m_params.initialGain;
                float f = m_params.baseFrequency;

                for (int o = 0; o < m_params.octaves; ++o) {
                    float n = Perlin3_Periodic(X * f, Y * f, Z * f, m_edgeSize, m_params.seed ^ 0xA5A5A5u); // ~[-1,1]
                    ps += a * n;
                    a *= m_params.gain;
                    f *= m_params.lacunarity;
                }

                float perlin01 = 0.5f + 0.5f * (ps / perlinNorm);
                if (perlin01 < 0.0f) perlin01 = 0.0f;
                if (perlin01 > 1.0f) perlin01 = 1.0f;

                // Worley-fBm für Kanäle
                float wR = WorleyFBM_Periodic(X, Y, Z, m_edgeSize, C_R, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0x51u);
                float G = WorleyFBM_Periodic(X, Y, Z, m_edgeSize, C_G, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0x1337u);
                float B = WorleyFBM_Periodic(X, Y, Z, m_edgeSize, C_B, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xBEEFu);
                float A = WorleyFBM_Periodic(X, Y, Z, m_edgeSize, C_A, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xCAFEu);

                // R = Perlin-Worley
                float R = perlin01 * wR;

                data[idx++] = std::clamp(R, 0.0f, 1.0f);
                data[idx++] = std::clamp(G, 0.0f, 1.0f);
                data[idx++] = std::clamp(B, 0.0f, 1.0f);
                data[idx++] = std::clamp(A, 0.0f, 1.0f);
            }
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
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, m_edgeSize, m_edgeSize, m_edgeSize, 0, GL_RGBA, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data())
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

    size_t voxelCount = size_t(m_edgeSize) * size_t(m_edgeSize) * size_t(m_edgeSize) * 4u;
    size_t expectedBytes = voxelCount * sizeof(float);

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

    size_t voxelCount = size_t(m_edgeSize) * size_t(m_edgeSize) * size_t(m_edgeSize) * 4u;
    size_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.Data()), bytes);
    return f.good();
}

// =================================================================================================

bool CloudVolume3D::Allocate(int edgeSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_edgeSize = edgeSize;
    m_data.Resize(size_t(edgeSize) * edgeSize * edgeSize);
    texBuf->m_info = TextureBuffer::BufferInfo(edgeSize, edgeSize, 1, GL_R16F, GL_RED);
    HasBuffer() = true;
    return true;
}


bool CloudVolume3D::Create(int edgeSize, const CloudVolumeParams& params, String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = GL_TEXTURE_3D;
    if (not Allocate(edgeSize))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        Compute();
        SaveToFile(noiseFilename);
    }
    Deploy();
    return true;
}


void CloudVolume3D::Compute(void) {
    float* data = m_data.Data();

    std::vector<int> perm;
    BuildPermutation(perm, m_edgeSize, m_params.seed ? m_params.seed : 0x9E3779B9u);

    float norm = 0.0f;
    {
        float a = m_params.initialGain;
        for (int o = 0; o < m_params.octaves; ++o) {
            norm += a;
            a *= m_params.gain;
        }
        if (norm <= 0.0f)
            norm = 1.0f;
    }

    size_t idx = 0;
#if NORMALIZE_NOISE
    float maxVal = 0.0f;
#endif
    for (int z = 0; z < m_edgeSize; ++z)
        for (int y = 0; y < m_edgeSize; ++y)
            for (int x = 0; x < m_edgeSize; ++x) {
                float u = (x + 0.5f) / float(m_edgeSize);
                float v = (y + 0.5f) / float(m_edgeSize);
                float w = (z + 0.5f) / float(m_edgeSize);
                // in periodischen Raum [0,m_edgeSize)
                float px = u * m_params.baseFreq;
                float py = v *  m_params.baseFreq;
                float pz = w *  m_params.baseFreq;

                float t = 0.0f;
                float a = m_params.initialGain;
                float f = 1.0f;

                for (int o = 0; o < m_params.octaves; ++o) {
                    float n = SNoise3Ashima(px * f, py * f, pz * f); // SNoise3Periodic(px * f, py * f, pz * f, perm, m_edgeSize); // ~[-1,1], m_edgeSize-periodisch
                    t += std::fabs(n) * a;
                    a *= m_params.gain;
                    f *= m_params.lacunarity;
                }

                float d = t / norm;              // ~0..1
                d = std::clamp(d, 0.0f, 1.0f);
                data[idx++] = d * 1.3f;
#if NORMALIZE_NOISE
                if (maxVal < d)
                    maxVal = d;
#endif
            }
#if NORMALIZE_NOISE
    if ((maxVal > 0.0f) and (maxVal < 0.999f)) {
        for (; idx; --idx, data++)
            *data /= maxVal;
    }
#endif
}


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
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, m_edgeSize, m_edgeSize, m_edgeSize, 0, GL_RED, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
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

    size_t voxelCount = size_t(m_edgeSize) * size_t(m_edgeSize) * size_t(m_edgeSize);
    size_t expectedBytes = voxelCount * sizeof(float);

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

    size_t voxelCount = size_t(m_edgeSize) * size_t(m_edgeSize) * size_t(m_edgeSize);
    size_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.Data()), bytes);
    return f.good();
}

// =================================================================================================
