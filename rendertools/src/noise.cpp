
#include <math.h>
#include <stdint.h>
#include <vector>

#include "array.hpp"
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

        struct Simplexi {
            GridPosi p[4];
        };

        struct Simplexf {
            GridPosf p[4];
        };

        inline const Simplexi& SelectSimplex(const GridPosf& p) {
            static const Simplexi lut[6] = {
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

        inline uint32_t Hash(const GridPosi& p) {
            return Hash(p.x, p.y, p.z);
        }
    };

    float SimplexPerlin(float x, float y, float z, int period) {
        const float F = 1.f / 3.f;
        const float G = 1.f / 6.f;

        float s = (x + y + z) * F;

        GridPosi base((int)floorf(x + s), (int)floorf(y + s), (int)floorf(z + s));
        float t = base.Sum() * G;

        Simplexf pos;
        pos.p[0] = (GridPosf(x, y, z) - base) + t;

        const Simplexi& offsets = SelectSimplex(pos.p[0]);
        pos.p[1] = (pos.p[0] - offsets.p[1]) + G;
        pos.p[2] = (pos.p[0] - offsets.p[2]) + G * 2.f;
        pos.p[3] = (pos.p[0] - GridPosi(1, 1, 1)) + G * 3.f;

        Simplexi corners;
        for (int i = 0; i < 4; ++i)
            corners.p[i] = base + offsets.p[i];


        if (period > 1) {
            for (int i = 0; i < 4; ++i)
                corners.p[i].Wrap(period);
        }

        auto contrib = [&](const GridPosf& p, uint32_t hash) {
            float falloff = 0.6f - p.Dot(p);
            if (falloff <= 0.f) 
                return 0.f;
            const grad3& g = gradLUT[hash % 12];
            float dot = g.x * p.x + g.y * p.y + g.z * p.z;
            falloff *= falloff;
            return falloff * falloff * dot;
            };

        float n = 0;
        for (int i = 0; i < 4; i++)
            n += contrib(pos.p[i], Hash(corners.p[i]));

        return 32.f * n;
    }

    // -------------------------------------------------------------------------------------------------

    namespace {
        GridPosf GradDecode(float p, const GridPosf& ns) {
            float h = p - 49.f * floorf(p * ns.z * ns.z);
            GridPosf g;
            g.x = floorf(h * ns.z);
            g.y = floorf(h - 7.f * g.x);
            g.x = g.x * ns.x + ns.y;
            g.y = g.y * ns.x + ns.y;
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

        Simplexf offsets;
        offsets.p[0] = (v - i) + t;

        GridPosf g{
            (offsets.p[0].x >= offsets.p[0].y) ? 1.f : 0.f,
            (offsets.p[0].y >= offsets.p[0].z) ? 1.f : 0.f,
            (offsets.p[0].z >= offsets.p[0].x) ? 1.f : 0.f
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

        offsets.p[1] = (offsets.p[0] - i1) + G;
        offsets.p[2] = (offsets.p[0] - i2) + F;
        offsets.p[3] = offsets.p[0] - 0.5f;

        // mod289(i)
        i.Mod289();

        // vec4 p
        float perm[4]{
            Permute(Permute(Permute(i.z + 0.f) + i.y + 0.f) + i.x + 0.f),
            Permute(Permute(Permute(i.z + i1.z) + i.y + i1.y) + i.x + i1.x),
            Permute(Permute(Permute(i.z + i2.z) + i.y + i2.y) + i.x + i2.x),
            Permute(Permute(Permute(i.z + 1.f) + i.y + 1.f) + i.x + 1.f)
        };

        const float m = 1.f / 7.f;
        GridPosf ns{ 2.f * m, 0.5f * m - 1.f, m };

        Simplexf gradients{
            GradDecode(perm[0], ns),
            GradDecode(perm[1], ns),
            GradDecode(perm[2], ns),
            GradDecode(perm[3], ns)
        };

        auto Normalize = [&](GridPosf& g) {
            g *= TaylorInvSqrt(g.Dot(g));
            };

        for (int i = 0; i < 4; i++)
            Normalize(gradients.p[i]);

        auto contrib = [&](const GridPosf& o, const GridPosf& g) {
            float falloff = 0.6f - o.x * o.x - o.y * o.y - o.z * o.z;
            if (falloff <= 0.f)
                return 0.f;
            falloff *= falloff;
            return falloff * falloff * g.Dot(o);
            };

        float n = 0;
        for (int i = 0; i < 4; ++i)
            n += contrib(offsets.p[i], gradients.p[i]);

        return 42.f * n;
    }

    // -------------------------------------------------------------------------------------------------

    inline float HashToUnit01(uint32_t h) {
        return (h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    // F1 (nächster Featurepunkt)
    float Worley(float x, float y, float z, int period) {
        GridPosf p(x, y, z);
        p.Wrap(period);

        GridPosi base((int)floorf(p.x), (int)floorf(p.y), (int)floorf(p.z));
        GridPosi c;

        float dMin = FLT_MAX;
        for (int dz = -1; dz <= 1; ++dz) {
            c.z = WrapInt(base.z + dz, period);
            for (int dy = -1; dy <= 1; ++dy) {
                c.y = WrapInt(base.y + dy, period);
                for (int dx = -1; dx <= 1; ++dx) {
                    c.x = WrapInt(base.x + dx, period);
                    uint32_t h = Hash(c);
                    GridPosf j(HashToUnit01(h), HashToUnit01(h * 0x9E3779B1u), HashToUnit01(h * 0xBB67AE85u));
                    GridPosf o = j + c; // ((float)c.x + jx, (float)c.y + jy, (float)c.z + jz);
                    GridPosf d = o - p;
                    float n = d.Dot(d);
                    if (n < dMin)
                        dMin = n;
                }
            }
        }

        float d = sqrtf(dMin) * (1.0f / 1.7320508075688772f);
        return std::clamp(d, 0.0f, 1.0f);
    }

    // -------------------------------------------------------------------------------------------------

    uint8_t Hash2iByte(int ix, int iy, uint32_t seed, uint32_t ch) {
        float phase = float((seed ^ (ch * 0x9E3779B9u)) & 0xFFFFu) * (1.0f / 65536.0f);
        float t = float(ix) * 127.1f + float(iy) * 311.7f + phase;
        float s = sinf(t) * 43758.5453f;
        float f = s - floorf(s);
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

};

// =================================================================================================
