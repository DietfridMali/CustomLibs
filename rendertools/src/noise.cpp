
#include <math.h>
#include <stdint.h>
#include <vector>

#include "noise.h"

// =================================================================================================

namespace Noise {

    namespace {
        struct grad3 { float x, y, z; };

        const grad3 gradLUT[12] = {
            { 1, 1, 0},{-1, 1, 0},{ 1,-1, 0},{-1,-1, 0},
            { 1, 0, 1},{-1, 0, 1},{ 1, 0,-1},{-1, 0,-1},
            { 0, 1, 1},{ 0,-1, 1},{ 0, 1,-1},{ 0,-1,-1}
        };

        // 5th degree smoothstep (better smoothing)
        inline float Fade(float t) {
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }

        // linear interpolation
        inline float Lerp(float a, float b, float t) {
            return a + t * (b - a);
        }

        inline uint32_t Hash(int x, int y, int z) {
            uint32_t h =
                (uint32_t)x * 374761393u
                + (uint32_t)y * 668265263u
                + (uint32_t)z * 2147483647u;
            h ^= h >> 13;
            h *= 1274126177u;
            h ^= h >> 16;
            return h;
        }

        inline float Dot(const GridPosf& a, const GridPosf& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
    };

    // -------------------------------------------------------------------------------------------------
    // 3D Perlin, nicht periodisch, ~[-1,1]
    namespace {
        inline grad3 RandomGradient(int ix, int iy, int iz) {
            uint32_t h = Hash(ix, iy, iz);
            return gradLUT[h % 12];
        }

        inline float DotGridGradient(int ix, int iy, int iz, float x, float y, float z) {
            grad3 g = RandomGradient(ix, iy, iz);
            float dx = x - (float)ix;
            float dy = y - (float)iy;
            float dz = z - (float)iz;
            return dx * g.x + dy * g.y + dz * g.z;
        }
    };

    float Perlin(float x, float y, float z) {
        int x0 = (int)floorf(x);
        int x1 = x0 + 1;
        int y0 = (int)floorf(y);
        int y1 = y0 + 1;
        int z0 = (int)floorf(z);
        int z1 = z0 + 1;

        float n000 = DotGridGradient(x0, y0, z0, x, y, z);
        float n100 = DotGridGradient(x1, y0, z0, x, y, z);
        float n010 = DotGridGradient(x0, y1, z0, x, y, z);
        float n110 = DotGridGradient(x1, y1, z0, x, y, z);
        float n001 = DotGridGradient(x0, y0, z1, x, y, z);
        float n101 = DotGridGradient(x1, y0, z1, x, y, z);
        float n011 = DotGridGradient(x0, y1, z1, x, y, z);
        float n111 = DotGridGradient(x1, y1, z1, x, y, z);

        float s = Fade(x - (float)x0);
        float nx00 = Lerp(n000, n100, s);
        float nx10 = Lerp(n010, n110, s);
        float nx01 = Lerp(n001, n101, s);
        float nx11 = Lerp(n011, n111, s);

        s = Fade(y - (float)y0);
        float nxy0 = Lerp(nx00, nx10, s);
        float nxy1 = Lerp(nx01, nx11, s);

        return Lerp(nxy0, nxy1, Fade(z - (float)z0));
    }

    // -------------------------------------------------------------------------------------------------

    void BuildPermutation(std::vector<int>& perm, int period, uint32_t seed) {
        perm.resize(period);
        for (int i = 0; i < period; ++i) perm[i] = i;
        uint32_t s = seed ? seed : 0x9E3779B9u;
        for (int i = period - 1; i > 0; --i) {
            s = s * 1664525u + 1013904223u;
            int j = int(s % uint32_t(i + 1));
            std::swap(perm[i], perm[j]);
        }
    }

    // -------------------------------------------------------------------------------------------------

    namespace {
        inline grad3 PermGradient(int x, int y, int z, const std::vector<int>& perm, int period) {
            int a = perm[WrapInt(x, period)];
            int b = perm[(a + WrapInt(y, period)) % period];
            int c = perm[(b + WrapInt(z, period)) % period];
            return gradLUT[c % 12];
        }

        inline float DotPermGradient(int ix, int iy, int iz, float x, float y, float z, const std::vector<int>& perm, int period) {
            grad3 g = PermGradient(ix, iy, iz, perm, period);
            float dx = x - (float)ix;
            float dy = y - (float)iy;
            float dz = z - (float)iz;
            return dx * g.x + dy * g.y + dz * g.z;
        }
    };

    float ImprovedPerlin(float x, float y, float z, const std::vector<int>& perm, int period) {
        int x0 = (int)floorf(x), x1 = x0 + 1;
        int y0 = (int)floorf(y), y1 = y0 + 1;
        int z0 = (int)floorf(z), z1 = z0 + 1;

        float n000 = DotPermGradient(x0, y0, z0, x, y, z, perm, period);
        float n100 = DotPermGradient(x1, y0, z0, x, y, z, perm, period);
        float n010 = DotPermGradient(x0, y1, z0, x, y, z, perm, period);
        float n110 = DotPermGradient(x1, y1, z0, x, y, z, perm, period);
        float n001 = DotPermGradient(x0, y0, z1, x, y, z, perm, period);
        float n101 = DotPermGradient(x1, y0, z1, x, y, z, perm, period);
        float n011 = DotPermGradient(x0, y1, z1, x, y, z, perm, period);
        float n111 = DotPermGradient(x1, y1, z1, x, y, z, perm, period);

        float sx = Fade(x - (float)x0);
        float sy = Fade(y - (float)y0);
        float sz = Fade(z - (float)z0);

        float nx00 = Lerp(n000, n100, sx);
        float nx10 = Lerp(n010, n110, sx);
        float nx01 = Lerp(n001, n101, sx);
        float nx11 = Lerp(n011, n111, sx);

        float nxy0 = Lerp(nx00, nx10, sy);
        float nxy1 = Lerp(nx01, nx11, sy);

        return Lerp(nxy0, nxy1, sz); // ~[-1,1]
    }

    // -------------------------------------------------------------------------------------------------

    namespace {

        struct SimplexOffset {
            int i, j, k;
        };

        struct Simplex {
            GridPosi o[4];
        };

        inline const Simplex& SelectSimplex(const GridPosf& p) {
            static const Simplex lut[6] = {
                {{{0,0,0},{1,0,0},{1,1,0},{1,1,1}}}, // X>=Y>=Z
                {{{0,0,0},{1,0,0},{1,0,1},{1,1,1}}}, // X>=Z>Y
                {{{0,0,0},{0,0,1},{1,0,1},{1,1,1}}}, // Z>X>=Y
                {{{0,0,0},{0,0,1},{0,1,1},{1,1,1}}}, // Z>Y>X
                {{{0,0,0},{0,1,0},{0,1,1},{1,1,1}}}, // Y>=Z>X
                {{{0,0,0},{0,1,0},{1,1,0},{1,1,1}}}  // Y>=X>=Z
            };
            if (p.x >= p.y) {
                if (p.y >= p.z) return lut[0];
                if (p.x >= p.z) return lut[1];
                return lut[2];
            }
            else {
                if (p.y < p.z)  return lut[3];
                if (p.x < p.z)  return lut[4];
                return lut[5];
            }
        }
    };

    float SimplexPerlin(float x, float y, float z, int period) {
        const float F = 1.f / 3.f;
        const float G = 1.f / 6.f;

        float s = (x + y + z) * F;
        int i = (int)floorf(x + s);
        int j = (int)floorf(y + s);
        int k = (int)floorf(z + s);

        float t = (i + j + k) * G;
        GridPosf p0(x - i + t, y - j + t, z - k + t);

        const Simplex& sp = SelectSimplex(p0);
        const GridPosi& o1 = sp.o[1];
        const GridPosi& o2 = sp.o[2];

        GridPosf p1(p0.x - o1.x + G, p0.y - o1.y + G, p0.z - o1.z + G);
        GridPosf p2(p0.x - o2.x + 2.f * G, p0.y - o2.y + 2.f * G, p0.z - o2.z + 2.f * G);
        GridPosf p3(p0.x - 1.f + 3.f * G, p0.y - 1.f + 3.f * G, p0.z - 1.f + 3.f * G);

        GridPosi lp[4] = {
            GridPosi(i + sp.o[0].x, j + sp.o[0].y, k + sp.o[0].z),
            GridPosi(i + sp.o[1].x, j + sp.o[1].y, k + sp.o[1].z),
            GridPosi(i + sp.o[2].x, j + sp.o[2].y, k + sp.o[2].z),
            GridPosi(i + sp.o[3].x, j + sp.o[3].y, k + sp.o[3].z)
        };

        if (period > 1) {
            for (int c = 0; c < 4; ++c)
                lp[c].Wrap(period);
        }

        uint32_t h0 = Hash(lp[0].x, lp[0].y, lp[0].z);
        uint32_t h1 = Hash(lp[1].x, lp[1].y, lp[1].z);
        uint32_t h2 = Hash(lp[2].x, lp[2].y, lp[2].z);
        uint32_t h3 = Hash(lp[3].x, lp[3].y, lp[3].z);

        auto contrib = [&](const GridPosf& p, uint32_t h) {
            float falloff = 0.6f - (p.x * p.x + p.y * p.y + p.z * p.z);
            if (falloff <= 0.f) 
                return 0.f;
            const grad3& g = gradLUT[h % 12];
            falloff *= falloff;
            return falloff * falloff * (g.x * p.x + g.y * p.y + g.z * p.z);
            };

        float n0 = contrib(p0, h0);
        float n1 = contrib(p1, h1);
        float n2 = contrib(p2, h2);
        float n3 = contrib(p3, h3);

        return 32.f * (n0 + n1 + n2 + n3);
    }

    // -------------------------------------------------------------------------------------------------

    namespace {
        GridPosf GradDecode(float p, const GridPosf& ns) {
            float h = p - 49.f * floorf(p * ns.z * ns.z);
            GridPosf g;
            g.x = floorf(h * ns.z) * ns.x + ns.y;
            g.y = floorf(h - 7.f * g.x) * ns.x + ns.y;
            g.z = 1.f - fabsf(g.x) - fabsf(g.y);
            float sx = (g.x < 0.f) ? -1.f : 1.f;
            float sy = (g.y < 0.f) ? -1.f : 1.f;
            if (g.z < 0.f) {
                g.x += sx;
                g.y += sy;
            }
            return g;
        }
    };

    float SimplexAshima(float x, float y, float z) {
        const float F = 1.f / 3.f;
        const float G = 1.f / 6.f;

        auto Permute = [&](float v) {
            return Mod289(((v * 34.f) + 1.f) * v);
            };

        auto TaylorInvSqrt = [](float r) {
            return 1.79284291400159f - 0.85373472095314f * r;
            };

        GridPosf v{ x, y, z };

        float s = (v.x + v.y + v.z) * F;
        GridPosf i{
            floorf(v.x + s),
            floorf(v.y + s),
            floorf(v.z + s)
        };

        float t = (i.x + i.y + i.z) * G;
        GridPosf x0 = (v - i) + t;

        GridPosf g{
            (x0.x >= x0.y) ? 1.f : 0.f,
            (x0.y >= x0.z) ? 1.f : 0.f,
            (x0.z >= x0.x) ? 1.f : 0.f
        };

        GridPosf l = 1.f - g;

        GridPosf i1{
            fminf(g.x, l.z),
            fminf(g.y, l.x),
            fminf(g.z, l.y)
        };
        GridPosf i2{
            fmaxf(g.x, l.z),
            fmaxf(g.y, l.x),
            fmaxf(g.z, l.y)
        };

        GridPosf x1 = (x0 - i1) + G;
        GridPosf x2 = (x0 - i2) + F;
        GridPosf x3 = x0 - 0.5f;

        // mod289(i)
        i.Mod289();

        // vec4 p
        float p0 = Permute(Permute(Permute(i.z + 0.f) + i.y + 0.f) + i.x + 0.f);
        float p1 = Permute(Permute(Permute(i.z + i1.z) + i.y + i1.y) + i.x + i1.x);
        float p2 = Permute(Permute(Permute(i.z + i2.z) + i.y + i2.y) + i.x + i2.x);
        float p3 = Permute(Permute(Permute(i.z + 1.f) + i.y + 1.f) + i.x + 1.f);

        const float n = 1.f / 7.f;
        GridPosf ns{ 2.f * n, 0.5f * n - 1.f, n };

        GridPosf g0 = GradDecode(p0, ns);
        GridPosf g1 = GradDecode(p1, ns);
        GridPosf g2 = GradDecode(p2, ns);
        GridPosf g3 = GradDecode(p3, ns);

        auto Normalize = [&](GridPosf& g) {
            g *= TaylorInvSqrt(g.Dot(g));
            };

        Normalize(g0);
        Normalize(g1);
        Normalize(g2);
        Normalize(g3);

        auto contrib = [&](const GridPosf& p, const GridPosf& g) {
            float tt = 0.6f - p.x * p.x - p.y * p.y - p.z * p.z;
            if (tt <= 0.f)
                return 0.f;
            tt *= tt;
            return tt * tt * g.Dot(p);
            };

        float n0 = contrib(x0, g0);
        float n1 = contrib(x1, g1);
        float n2 = contrib(x2, g2);
        float n3 = contrib(x3, g3);

        return 42.f * (n0 + n1 + n2 + n3);
    }

    // -------------------------------------------------------------------------------------------------

    inline float HashToUnit01(uint32_t h) {
        return (h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    // F1 (nächster Featurepunkt)
    float Worley(const GridPosf& p, int period) {
        GridPosf pf = p;
        if (period > 1)
            pf.Wrap(period);

        GridPosi base(
            (int)floorf(pf.x),
            (int)floorf(pf.y),
            (int)floorf(pf.z)
        );

        float d2min = FLT_MAX;

        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    GridPosi c(base.x + dx, base.y + dy, base.z + dz);
                    if (period > 1)
                        c.Wrap(period);

                    uint32_t h = Hash(c.x, c.y, c.z);
                    float jx = HashToUnit01(h);
                    float jy = HashToUnit01(h * 0x9E3779B1u);
                    float jz = HashToUnit01(h * 0xBB67AE85u);

                    GridPosf fp((float)c.x + jx, (float)c.y + jy, (float)c.z + jz);
                    GridPosf d(fp.x - pf.x, fp.y - pf.y, fp.z - pf.z);

                    float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
                    if (d2 < d2min) d2min = d2;
                }

        float d = sqrtf(d2min) * (1.0f / 1.7320508075688772f);
        if (d < 0.f) d = 0.f;
        if (d > 1.f) d = 1.f;
        return d;
    }

};

// =================================================================================================
