
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

        inline int Wrap(int x, int period) {
            int r = x % period;
            return r < 0 ? r + period : r;
        }

        inline grad3 PermGradient(int x, int y, int z, const std::vector<int>& perm, int period) {
            int a = perm[Wrap(x, period)];
            int b = perm[(a + Wrap(y, period)) % period];
            int c = perm[(b + Wrap(z, period)) % period];
            return gradLUT[c % 12];
        }

        inline float DotPermGradient(int ix, int iy, int iz, float x, float y, float z, const std::vector<int>& perm, int period) {
            grad3 g = PermGradient(ix, iy, iz, perm, period);
            float dx = x - (float)ix;
            float dy = y - (float)iy;
            float dz = z - (float)iz;
            return dx * g.x + dy * g.y + dz * g.z;
        }

        struct gridPos {
            float x, y, z;
        };

        struct SimplexOffset { 
            int i, j, k; 
        };

        struct Simplex { 
            SimplexOffset o[4]; 
        };

        inline const Simplex& SelectSimplex(const gridPos& p) {
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

        gridPos GradDecode(float p, gridPos& ns) {
            float h = p - 49.f * floorf(p * ns.z * ns.z);
            float x = floorf(h * ns.z);
            float y = floorf(h - 7.f * x);
            gridPos g;
            g.x = x * ns.x + ns.y;
            g.y = y * ns.x + ns.y;
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

    // -------------------------------------------------------------------------------------------------
    // 3D Perlin, nicht periodisch, ~[-1,1]

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

    inline float SimplexPerlin(float x, float y, float z, int period) {
        const float F = 1.f / 3.f;
        const float G = 1.f / 6.f;

        float s = (x + y + z) * F;
        int i = (int)floorf(x + s);
        int j = (int)floorf(y + s);
        int k = (int)floorf(z + s);

        float t = (i + j + k) * G;
        gridPos p0{ x - i + t, y - j + t, z - k + t };

        const Simplex& sp = SelectSimplex(p0);

        gridPos p1{
            p0.x - sp.o[1].i + G,
            p0.y - sp.o[1].j + G,
            p0.z - sp.o[1].k + G
        };
        gridPos p2{
            p0.x - sp.o[2].i + 2.f * G,
            p0.y - sp.o[2].j + 2.f * G,
            p0.z - sp.o[2].k + 2.f * G
        };
        gridPos p3{
            p0.x - 1.f + 3.f * G,
            p0.y - 1.f + 3.f * G,
            p0.z - 1.f + 3.f * G
        };

        SimplexOffset lp[4] = {
            { i + sp.o[0].i, j + sp.o[0].j, k + sp.o[0].k },
            { i + sp.o[1].i, j + sp.o[1].j, k + sp.o[1].k },
            { i + sp.o[2].i, j + sp.o[2].j, k + sp.o[2].k },
            { i + sp.o[3].i, j + sp.o[3].j, k + sp.o[3].k }
        };

        if (period > 1) {
            for (int c = 0; c < 4; ++c) {
                lp[c].i = Wrap(lp[c].i, period);
                lp[c].j = Wrap(lp[c].j, period);
                lp[c].k = Wrap(lp[c].k, period);
            }
        }

        uint32_t h0 = Hash(lp[0].i, lp[0].j, lp[0].k);
        uint32_t h1 = Hash(lp[1].i, lp[1].j, lp[1].k);
        uint32_t h2 = Hash(lp[2].i, lp[2].j, lp[2].k);
        uint32_t h3 = Hash(lp[3].i, lp[3].j, lp[3].k);

        auto contrib = [&](const gridPos& p, uint32_t h) {
            float tt = 0.6f - (p.x * p.x + p.y * p.y + p.z * p.z);
            if (tt <= 0.f) return 0.f;
            tt *= tt;
            const grad3& g = gradLUT[h % 12];
            return tt * tt * (g.x * p.x + g.y * p.y + g.z * p.z);
            };

        float n0 = contrib(p0, h0);
        float n1 = contrib(p1, h1);
        float n2 = contrib(p2, h2);
        float n3 = contrib(p3, h3);

        return 32.f * (n0 + n1 + n2 + n3);
    }

    // -------------------------------------------------------------------------------------------------

    float SimplexAshima(float x, float y, float z) {
        const float Cx = 1.f / 6.f;
        const float Cy = 1.f / 3.f;

        auto Mod289 = [](float v) {
            return v - floorf(v * (1.f / 289.f)) * 289.f;
            };

        auto Permute = [&](float v) {
            return Mod289(((v * 34.f) + 1.f) * v);
            };

        auto TaylorInvSqrt = [](float r) {
            return 1.79284291400159f - 0.85373472095314f * r;
            };

        auto Dot = [](const gridPos& a, const gridPos& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
            };

        gridPos v{ x, y, z };

        float s = (v.x + v.y + v.z) * Cy;
        gridPos i{
            floorf(v.x + s),
            floorf(v.y + s),
            floorf(v.z + s)
        };

        float t = (i.x + i.y + i.z) * Cx;
        gridPos x0{
            v.x - i.x + t,
            v.y - i.y + t,
            v.z - i.z + t
        };

        float gx = (x0.x >= x0.y) ? 1.f : 0.f;
        float gy = (x0.y >= x0.z) ? 1.f : 0.f;
        float gz = (x0.z >= x0.x) ? 1.f : 0.f;

        float lx = 1.f - gx;
        float ly = 1.f - gy;
        float lz = 1.f - gz;

        gridPos i1{
            fminf(gx, lz),
            fminf(gy, lx),
            fminf(gz, ly)
        };
        gridPos i2{
            fmaxf(gx, lz),
            fmaxf(gy, lx),
            fmaxf(gz, ly)
        };

        gridPos x1{
            x0.x - i1.x + Cx,
            x0.y - i1.y + Cx,
            x0.z - i1.z + Cx
        };
        gridPos x2{
            x0.x - i2.x + Cy,
            x0.y - i2.y + Cy,
            x0.z - i2.z + Cy
        };
        gridPos x3{
            x0.x - 0.5f,
            x0.y - 0.5f,
            x0.z - 0.5f
        };

        // mod289(i)
        i.x = Mod289(i.x);
        i.y = Mod289(i.y);
        i.z = Mod289(i.z);

        // vec4 p
        float p0 = Permute(Permute(Permute(i.z + 0.f) + i.y + 0.f) + i.x + 0.f);
        float p1 = Permute(Permute(Permute(i.z + i1.z) + i.y + i1.y) + i.x + i1.x);
        float p2 = Permute(Permute(Permute(i.z + i2.z) + i.y + i2.y) + i.x + i2.x);
        float p3 = Permute(Permute(Permute(i.z + 1.f) + i.y + 1.f) + i.x + 1.f);

        const float n = 1.f / 7.f;
        gridPos ns{ 2.f * n, 0.5f * n - 1.f, n };

        gridPos g0 = GradDecode(p0, ns);
        gridPos g1 = GradDecode(p1, ns);
        gridPos g2 = GradDecode(p2, ns);
        gridPos g3 = GradDecode(p3, ns);

        auto Normalize = [&](gridPos& g) {
            float i = TaylorInvSqrt(g.x * g.x + g.y * g.y + g.z * g.z);
            g.x *= i;
            g.y *= i;
            g.z *= i;
            };

        Normalize(g0);
        Normalize(g1);
        Normalize(g2);
        Normalize(g3);

        auto contrib = [&](const gridPos& p, const gridPos& g) {
            float tt = 0.6f - p.x * p.x - p.y * p.y - p.z * p.z;
            if (tt <= 0.f)
                return 0.f;
            tt *= tt;
            return tt * tt * (g.x * p.x + g.y * p.y + g.z * p.z);
            };

        float n0 = contrib(x0, g0);
        float n1 = contrib(x1, g1);
        float n2 = contrib(x2, g2);
        float n3 = contrib(x3, g3);

        return 42.f * (n0 + n1 + n2 + n3);
    }
};

// =================================================================================================
