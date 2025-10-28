
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "noisetexture.h"
#include "base_renderer.h"

// =================================================================================================
/* f�r volumetrische Wolken:
    NoiseTexture3D shapeTex3D, detailTex3D;

    Noise3DParams pShape;
    pShape.seed = 0xA5A5A5u;
    pShape.base = 2.032f;   // grobe Strukturen
    pShape.lac = 2.3f;
    pShape.oct = 5;
    pShape.warp = 0.08f;

    Noise3DParams pDetail = pShape;
    pDetail.seed = 0x9e3779b9u;
    pDetail.base = 0.6f;    // h�here Frequenz
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
    perm.resize(period); for (int i = 0; i < period; ++i) perm[i] = i;
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
    m_data.Resize(size_t(edgeSize) * edgeSize * edgeSize);
    // Werte sind hier irrelevant, dienen nur dem internen Status
    texBuf->m_info = TextureBuffer::BufferInfo(edgeSize, edgeSize, 1, GL_R16F, GL_RED);
    HasBuffer() = true;
    return true;
}


bool NoiseTexture3D::Create(int edgeSize, const Noise3DParams& params) {
    if (not Texture::Create()) 
        return false;
    m_type = GL_TEXTURE_3D;
    if (not Allocate(edgeSize)) 
        return false;
    m_params = params;
    ComputeNoise();
    Deploy();
    return true;
}


// ComputeNoise parametrisierbar gemacht (deine Simplex-FBM + Warp)
void NoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.Data();
    std::vector<int> perm;
    BuildPermutation(perm, m_edgeSize, m_params.seed);

    float rotc = cosf(m_params.rot_deg * 3.14159265f / 180.f);
    float rots = sinf(m_params.rot_deg * 3.14159265f / 180.f);
    float scaleBase = float(m_edgeSize) / m_params.base;

    float norm = m_params.init_gain * (1.f - powf(m_params.gain, float(m_params.oct))) / (1.f - m_params.gain);
    if (norm <= 0.f) norm = 1.f;

    size_t idx = 0;
    for (int z = 0; z < m_edgeSize; ++z)
        for (int y = 0; y < m_edgeSize; ++y)
            for (int x = 0; x < m_edgeSize; ++x) {
                float X = x / scaleBase, Y = y / scaleBase, Z = z / scaleBase;

                float Xr = rotc * X - rots * Z;
                float Yr = Y;
                float Zr = rots * X + rotc * Z;

                float wx = SNoise3Periodic(Xr * 2.31f, Yr * 2.31f, Zr * 2.31f, perm, m_edgeSize);
                float wy = SNoise3Periodic(Xr * 2.31f + 17.f, Yr * 2.31f, Zr * 2.31f, perm, m_edgeSize);
                float wz = SNoise3Periodic(Xr * 2.31f, Yr * 2.31f + 11.f, Zr * 2.31f, perm, m_edgeSize);
                Xr += m_params.warp * wx; 
                Yr += m_params.warp * wy; 
                Zr += m_params.warp * wz;

                float s = 0.f, a = m_params.init_gain, f = 1.f;
                for (int o = 0; o < m_params.oct; ++o) {
                    s += a * fabsf(SNoise3Periodic(Xr * f, Yr * f, Zr * f, perm, m_edgeSize));
                    a *= m_params.gain; 
                    f *= m_params.lac;
                }
                s /= norm;
                data[idx++] = std::clamp((s - 0.28f) * 1.9f, 0.0f, 1.0f);
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
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, m_edgeSize, m_edgeSize, m_edgeSize, 0, GL_RED, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        glGenerateMipmap(GL_TEXTURE_3D);
        Release();
    }
}

// =================================================================================================
