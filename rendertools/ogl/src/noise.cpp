
#include <math.h>
#include <stdint.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "std_defines.h"
#include "vector.hpp"
#include "conversions.hpp"

#include "noise.h"

// =================================================================================================

namespace Noise {

    static uint32_t hashSeed = 0x1234567u;

    inline void SetHashSeed(uint32_t seed = 0xA341316Cu) {
        hashSeed = seed;
    }


    namespace {
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

        inline uint32_t Hash(int x, int y, int z, uint32_t seed = 0xA341316Cu) {
            uint32_t h =
                x * 374761393u ^
                y * 668265263u ^
                z * 2147483647u ^
                (seed * 0x9E3779B9u);
            h ^= h >> 13;
            h *= 1274126177u;
            h ^= h >> 16;
            return h;
        }


        inline float fractf(float x) {
            return x - std::floor(x);
        }

        inline Vector2f Hash23(int ix, int iy, int iz) {
            float n1 = ix * 127.1f + iy * 311.7f + iz * 74.7f;
            float n2 = ix * 269.5f + iy * 183.3f + iz * 246.1f;
            float s1 = sinf(n1) * 43758.5453123f;
            float s2 = sinf(n2) * 43758.5453123f;
            return Vector2f(fractf(s1), fractf(s2));
        }

        inline float Dot(const GridPosf& a, const GridPosf& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
    };

    // -------------------------------------------------------------------------------------------------
    // 3D Perlin, nicht periodisch, ~[-1,1]

    void PerlinNoise::Setup(int period, uint32_t seed) {
        m_period = period;
        m_seed = seed;
    }

    Noise::grad3 PerlinNoise::Gradient(int ix, int iy, int iz) {
#if 0
        Vector2f u = Hash23(ix, iy, iz);      // u.x, u.y in [0,1)
        float z = 2.0f * u.x - 1.0f;
        float a = float(TWO_PI * u.y);
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return grad3(r * cos(a), r * sin(a), z);
#else
        uint32_t h = Hash(ix, iy, iz, m_seed);
        return gradLUT[h % 12];
#endif
    }

    float PerlinNoise::GradientDot(int ix, int iy, int iz) {
        grad3 g = Gradient(ix % m_period, iy % m_period, iz % m_period);
        float dx = m_p.x - (float)ix;
        float dy = m_p.y - (float)iy;
        float dz = m_p.z - (float)iz;
        return dx * g.x + dy * g.y + dz * g.z;
    }

    float PerlinNoise::Compute(Vector3f p)  {
        m_p = p;

        int x0 = (int)floorf(p.x),
            x1 = x0 + 1;
        int y0 = (int)floorf(p.y),
            y1 = y0 + 1;
        int z0 = (int)floorf(p.z),
            z1 = z0 + 1;

        float n000 = GradientDot(x0, y0, z0);
        float n100 = GradientDot(x1, y0, z0);
        float n010 = GradientDot(x0, y1, z0);
        float n110 = GradientDot(x1, y1, z0);
        float n001 = GradientDot(x0, y0, z1);
        float n101 = GradientDot(x1, y0, z1);
        float n011 = GradientDot(x0, y1, z1);
        float n111 = GradientDot(x1, y1, z1);

        float s = Fade(p.x - (float)x0);
        float nx00 = Lerp(n000, n100, s);
        float nx10 = Lerp(n010, n110, s);
        float nx01 = Lerp(n001, n101, s);
        float nx11 = Lerp(n011, n111, s);

        s = Fade(p.y - (float)y0);
        float nxy0 = Lerp(nx00, nx10, s);
        float nxy1 = Lerp(nx01, nx11, s);

        return Lerp(nxy0, nxy1, Fade(p.z - (float)z0));
    }

    // -------------------------------------------------------------------------------------------------

    void ImprovedPerlinNoise::Setup(int period, uint32_t seed) {
        m_period = period;
        m_seed = seed ? seed : 0x9E3779B9u;
        BuildPermutation();
    }

    void ImprovedPerlinNoise::BuildPermutation(void) {
        m_perm.resize(m_period);
        for (int i = 0; i < m_period; ++i) 
            m_perm[i] = i;
        uint32_t s = m_seed;
        for (int i = m_period - 1; i > 0; --i) {
            s = s * 1664525u + 1013904223u;
            int j = int(s % uint32_t(i + 1));
            std::swap(m_perm[i], m_perm[j]);
        }
    }

    Noise::grad3 ImprovedPerlinNoise::Gradient(int x, int y, int z) {
        int i = m_perm[(m_perm[(m_perm[x % m_period] + y) % m_period] + z) % m_period];
        return gradLUT[i % 12];
    }

    float ImprovedPerlinNoise::GradientDot(int ix, int iy, int iz) {
        grad3 g = Gradient(ix, iy, iz);
        float dx = m_p.x - (float)ix;
        float dy = m_p.y - (float)iy;
        float dz = m_p.z - (float)iz;
        return dx * g.x + dy * g.y + dz * g.z;
    }

    float ImprovedPerlinNoise::Compute(Vector3f& p) {
        m_p = p;
    
        int x0 = (int)floorf(p.x),
            x1 = x0 + 1;
        int y0 = (int)floorf(p.y),
            y1 = y0 + 1;
        int z0 = (int)floorf(p.z),
            z1 = z0 + 1;

        float n000 = GradientDot(x0, y0, z0);
        float n100 = GradientDot(x1, y0, z0);
        float n010 = GradientDot(x0, y1, z0);
        float n110 = GradientDot(x1, y1, z0);
        float n001 = GradientDot(x0, y0, z1);
        float n101 = GradientDot(x1, y0, z1);
        float n011 = GradientDot(x0, y1, z1);
        float n111 = GradientDot(x1, y1, z1);

        float sx = Fade(m_p.x - (float)x0);
        float sy = Fade(m_p.y - (float)y0);
        float sz = Fade(m_p.z - (float)z0);

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

        inline uint32_t Hash(const GridPosi& p, uint32_t seed) {
            return Hash(p.x, p.y, p.z, seed);
        }
    };

    float SimplexPerlin(Vector3f p, uint32_t seed) {
        const float F = 1.f / 3.f;
        const float G = 1.f / 6.f;

        float s = (p.x + p.y + p.z) * F;

        GridPosi base((int)floorf(p.x + s), (int)floorf(p.y + s), (int)floorf(p.z + s));
        float t = base.Sum() * G;

        Simplexf pos;
        pos.p[0] = (GridPosf(p.x, p.y, p.z) - base) + t;

        const Simplexi& offsets = SelectSimplex(pos.p[0]);
        pos.p[1] = (pos.p[0] - offsets.p[1]) + G;
        pos.p[2] = (pos.p[0] - offsets.p[2]) + G * 2.f;
        pos.p[3] = (pos.p[0] - GridPosi(1, 1, 1)) + G * 3.f;

        Simplexi corners;
        for (int i = 0; i < 4; ++i)
            corners.p[i] = base + offsets.p[i];

        auto contrib = [&](const GridPosf& pos, uint32_t hash) {
            float falloff = 0.6f - pos.Dot(pos);
            if (falloff <= 0.f) 
                return 0.f;
            const grad3& grad = gradLUT[hash % 12];
            float dot = grad.x * pos.x + grad.y * pos.y + grad.z * pos.z;
            falloff *= falloff;
            return falloff * falloff * dot;
            };

        float n = 0;
        for (int i = 0; i < 4; i++)
            n += contrib(pos.p[i], Hash(corners.p[i], seed));

        return 32.f * n;
    }


    inline float WrapFloat(float v, int period) {
        float iv = floorf(v);
        float fv = v - iv;
        int wi = WrapInt((int)iv, period);
        return (float)wi + fv;
    }


    float PeriodicSimplexPerlin(Vector3f p, int period, uint32_t seed) {
        if (period > 1) {
            p.x = WrapFloat(p.x, period);
            p.y = WrapFloat(p.y, period);
            p.z = WrapFloat(p.z, period);
        }

        // interne Periodenlogik hier AUS (period = 0 oder ganz weglassen)
        return SimplexPerlin(p, seed);
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

    float SimplexAshima(Vector3f p) {
        const float F = 1.f / 3.f;
        const float G = 1.f / 6.f;

        auto Permute = [&](float v) {
            return Mod289(((v * 34.f) + 1.f) * v);
            };

        auto TaylorInvSqrt = [](float r) {
            return 1.79284291400159f - 0.85373472095314f * r;
            };

        GridPosf pos{ p.x, p.y, p.z };

        float s = (pos.x + pos.y + pos.z) * F;
        GridPosf i{
            floorf(pos.x + s),
            floorf(pos.y + s),
            floorf(pos.z + s)
        };

        float t = (i.x + i.y + i.z) * G;

        Simplexf offsets;
        offsets.p[0] = (pos - i) + t;

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
            float falloff = 0.6f - o.Dot(o);
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


    float PeriodicSimplexAshima(Vector3f p, int period) {
        if (period > 0.f) {
            p.x = WrapFloat(p.x, period);
            p.y = WrapFloat(p.y, period);
            p.z = WrapFloat(p.z, period);
        }
        return SimplexAshima(p);
    }

    // -------------------------------------------------------------------------------------------------

    inline float HashToUnit01(uint32_t h) {
        return (h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    // F1 (nächster Featurepunkt)
    float Worley(Vector3f p, int period, uint32_t seed) {
        GridPosf pos(p.x, p.y, p.z);
        pos.Wrap(period);

        GridPosi base((int)floorf(pos.x), (int)floorf(pos.y), (int)floorf(pos.z));
        GridPosi c;

        float dMin = FLT_MAX;
        for (int dz = -1; dz <= 1; ++dz) {
            c.z = WrapInt(base.z + dz, period);
            for (int dy = -1; dy <= 1; ++dy) {
                c.y = WrapInt(base.y + dy, period);
                for (int dx = -1; dx <= 1; ++dx) {
                    c.x = WrapInt(base.x + dx, period);

                    uint32_t h = Hash(c, seed);
                    GridPosf j(
                        HashToUnit01(h),
                        HashToUnit01(h * 0x9E3779B1u),
                        HashToUnit01(h * 0xBB67AE85u)
                    );

                    GridPosf o = j + GridPosf((float)c.x, (float)c.y, (float)c.z);
                    GridPosf d = o - pos;

                    // Periodische Distanz (Minimal-Image-Konvention)
                    if (d.x > 0.5f * period) 
                        d.x -= period;
                    if (d.x < -0.5f * period) 
                        d.x += period;
                    if (d.y > 0.5f * period) 
                        d.y -= period;
                    if (d.y < -0.5f * period) 
                        d.y += period;
                    if (d.z > 0.5f * period) 
                        d.z -= period;
                    if (d.z < -0.5f * period) 
                        d.z += period;

                    float n = d.Dot(d);
                    if (n < dMin) dMin = n;
                }
            }
        }

        float d = sqrtf(dMin) * (1.0f / 1.7320508075688772f);
        return std::clamp(d, 0.0f, 1.0f);
    }

    // -------------------------------------------------------------------------------------------------


#if 1
    using glm::vec2;
    using glm::vec3;
    using glm::vec4;

#define xyz(v)   (v)
#define xxx(v)   vec3(v.x, v.x, v.x)
#define yyy(v)   vec3(v.y, v.y, v.y)
#define xzx(v)   vec3(v.x, v.z, v.x)
#define yzx(v)   vec3(v.y, v.z, v.x)
#define zxy(v)   vec3(v.z, v.x, v.y)
#define wyz(v)   vec3(v.w, v.y, v.z)
#define xxyy(v)  vec4(v.x, v.x, v.y, v.y)
#define yyyy(v)  vec4(v.y, v.y, v.y, v.y)
#define xzyw(v)  vec4(v.x, v.z, v.y, v.w)
#define xyzw(v)  (v)
#define zzww(v)  vec4(v.z, v.z, v.w, v.w)

    vec4 mul(vec4 v, float n) {
        return vec4(v.x * n, v.y * n, v.z * n, v.w * n);
    }

    vec4 add(vec4 v, float n) {
        return vec4(v.x + n, v.y + n, v.z + n, v.w + n);
    }

    vec3 absv(vec3 v) {
        return vec3(fabs(v.x), fabs(v.y), fabs(v.z)); 
    }
    vec4 absv(vec4 v) {
        return vec4(fabs(v.x), fabs(v.y), fabs(v.z), fabs(v.w));
    }

    vec4 floorv(vec4 v) {
        return vec4(floor(v.x), floor(v.y), floor(v.z), floor(v.w));
    }

    vec3 floorv(vec3 v) {
        return vec3(floor(v.x), floor(v.y), floor(v.z));
    }

    vec3 sub(float n, vec3 v) {
        return vec3(n - v.x, n - v.y, n - v.z);
    }

    vec4 sub(float n, vec4 v) {
        return vec4(n - v.x, n - v.y, n - v.z, n - v.w);
    }

    float step(float edge, float v) {
        return (v < edge) ? 0.0f : 1.0f;
    }

    vec4 maxv(vec4 v, float n) {
        return vec4(std::max(v.x, n), std::max(v.y, n), std::max(v.z, n), std::max(v.w, n));
    }

    float mod289(float x) { return x - floor(x * (1.0f / 289.0f)) * 289.0f; }

    vec3 mod289(vec3 x) {
        return vec3(mod289(x.x), mod289(x.y), mod289(x.z));
    }

    vec4 mod289(vec4 x) {
        return vec4(mod289(x.x), mod289(x.y), mod289(x.z), mod289(x.w));
    }

    vec4 permute(vec4 x) { return mod289((add(mul(x, 34.0), 1.0)) * x); }

    vec4 taylorInvSqrt(vec4 r) { return add(mul(r, -0.85373472095314f), 1.79284291400159f); }

    float SimplexAshimaGLSL(Vector3f pos) {
        vec3 v(pos.x, pos.y, pos.z);
        const Vector2f C = Vector2f(1.0f / 6.0f, 1.0f / 3.0f);
        const vec4 D = vec4(0.0f, 0.5, 1.0f, 2.0f);
        vec3 i = floor(v + dot(v, yyy(C)));
        vec3 x0 = v - i + dot(i, xxx(C));
        vec3 g = step(yzx(x0), xyz(x0)), 
             l = sub(1.0f, g);
        vec3 i1 = min(xyz(g), zxy(l)),
             i2 = max(xyz(g), zxy(l));
        vec3 x1 = x0 - i1 + xxx(C),
             x2 = x0 - i2 + yyy(C),
             x3 = x0 - yyy(D);
        i = mod289(i);
        vec4 p = permute(
            permute(
                permute(add(vec4(0.0f, i1.z, i2.z, 1.0f), i.z))
                + add(vec4(0.0f, i1.y, i2.y, 1.0f), i.y))
            + add(vec4(0.0f, i1.x, i2.x, 1.0f), i.x));
        float n_ = 0.142857142857f;
        vec3  ns = n_ * wyz(D) - xzx(D);
        vec4 j = p - mul(floorv(mul(p,ns.z * ns.z)), 49.0f);
        vec4 x_ = floorv(mul(j, ns.z)),
             y_ = floorv(j - mul(x_, 7.0f));
        vec4 x = mul(x_, ns.x) + yyyy(ns),
             y = mul(y_, ns.x) + yyyy(ns);
        vec4 h = 1.0f - absv(x) - absv(y);
        vec4 b0 = vec4(x.x, x.y, y.x, y.y),
             b1 = vec4(x.z, x.w, y.z, y.w);
        vec4 s0 = add(mul(floorv(b0), 2.0f), 1.0f),
             s1 = add(mul(floorv(b1), 2.0f), 1.0f);
        vec4 sh = -step(h, vec4(0.0f));
        vec4 a0 = xzyw(b0) + xzyw(s0) * xxyy(sh);
        vec4 a1 = xzyw(b1) + xzyw(s1) * zzww(sh);
        vec3 p0 = vec3(a0.x, a0.y, h.x), p1 = vec3(a0.z, a0.w, h.y);
        vec3 p2 = vec3(a1.x, a1.y, h.z), p3 = vec3(a1.z, a1.w, h.w);
        vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
        p0 *= norm.x;
        p1 *= norm.y;
        p2 *= norm.z;
        p3 *= norm.w;
        vec4 m = maxv(sub(0.6f, vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3))), 0.0f);
        m *= m;
        return 42.0f * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
    }
#endif

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

    // -------------------------------------------------------------------------------------------------

#define NOISE_TYPE 0

    using glm::vec2;
    using glm::vec3;
    using glm::vec4;

    static constexpr uint32_t UI0 = 1597334673u;
    static constexpr uint32_t UI1 = 3812015801u;
    static constexpr uint32_t UI2 = 2798796415u;
    static constexpr float UIF = 1.0f / 4294967295.0f;

    void CloudNoise::Setup(int basePeriod, uint32_t perlinSeed, uint32_t worleySeed) {
        m_perlin.Setup(basePeriod, perlinSeed);
        m_perlinSeed = perlinSeed;
        m_worleySeed = worleySeed;
    }
    

    float CloudNoise::Remap(float x, float oldMin, float oldMax, float newMin, float newMax) {
        return ((x - oldMin) / (oldMax - oldMin)) * (newMax - newMin) + newMin;
    }


    vec3 CloudNoise::Hash33(vec3 p) {
        int xi = static_cast<int>(p.x);
        int yi = static_cast<int>(p.y);
        int zi = static_cast<int>(p.z);

        uint32_t ux = static_cast<uint32_t>(xi) * UI0;
        uint32_t uy = static_cast<uint32_t>(yi) * UI1;
        uint32_t uz = static_cast<uint32_t>(zi) * UI2;

        uint32_t n = ux ^ uy ^ uz;

        ux = n * UI0;
        uy = n * UI1;
        uz = n * UI2;

        float fx = static_cast<float>(ux) * UIF;
        float fy = static_cast<float>(uy) * UIF;
        float fz = static_cast<float>(uz) * UIF;

        return vec3(
            -1.0f + 2.0f * fx,
            -1.0f + 2.0f * fy,
            -1.0f + 2.0f * fz
        );
    }


    float CloudNoise::GradientNoise(vec3 x, float freq) {
        vec3 p = glm::floor(x);
        vec3 w = glm::fract(x);

        vec3 u = w * w * w * (w * (w * 6.0f - 15.0f) + 10.0f);

        vec3 ga = Hash33(glm::mod(p + vec3(0.0f, 0.0f, 0.0f), freq));
        vec3 gb = Hash33(glm::mod(p + vec3(1.0f, 0.0f, 0.0f), freq));
        vec3 gc = Hash33(glm::mod(p + vec3(0.0f, 1.0f, 0.0f), freq));
        vec3 gd = Hash33(glm::mod(p + vec3(1.0f, 1.0f, 0.0f), freq));
        vec3 ge = Hash33(glm::mod(p + vec3(0.0f, 0.0f, 1.0f), freq));
        vec3 gf = Hash33(glm::mod(p + vec3(1.0f, 0.0f, 1.0f), freq));
        vec3 gg = Hash33(glm::mod(p + vec3(0.0f, 1.0f, 1.0f), freq));
        vec3 gh = Hash33(glm::mod(p + vec3(1.0f, 1.0f, 1.0f), freq));

        float va = glm::dot(ga, w - vec3(0.0f, 0.0f, 0.0f));
        float vb = glm::dot(gb, w - vec3(1.0f, 0.0f, 0.0f));
        float vc = glm::dot(gc, w - vec3(0.0f, 1.0f, 0.0f));
        float vd = glm::dot(gd, w - vec3(1.0f, 1.0f, 0.0f));
        float ve = glm::dot(ge, w - vec3(0.0f, 0.0f, 1.0f));
        float vf = glm::dot(gf, w - vec3(1.0f, 0.0f, 1.0f));
        float vg = glm::dot(gg, w - vec3(0.0f, 1.0f, 1.0f));
        float vh = glm::dot(gh, w - vec3(1.0f, 1.0f, 1.0f));

        return va +
            u.x * (vb - va) +
            u.y * (vc - va) +
            u.z * (ve - va) +
            u.x * u.y * (va - vb - vc + vd) +
            u.y * u.z * (va - vc - ve + vg) +
            u.z * u.x * (va - vb - ve + vf) +
            u.x * u.y * u.z * (-va + vb + vc - vd + ve - vf - vg + vh);
    }

#if !NOISE_TYPE

    float CloudNoise::WorleyNoise(const Vector3f& p, float freq) {
        int period = (int)freq;

        GridPosf pos(p.x, p.y, p.z);
        pos.Wrap(period);

        GridPosi base((int)std::floor(pos.x),
            (int)std::floor(pos.y),
            (int)std::floor(pos.z));
        GridPosi c;

        float minDist = FLT_MAX;

        for (int dz = -1; dz <= 1; ++dz) {
            c.z = WrapInt(base.z + dz, period);
            for (int dy = -1; dy <= 1; ++dy) {
                c.y = WrapInt(base.y + dy, period);
                for (int dx = -1; dx <= 1; ++dx) {
                    c.x = WrapInt(base.x + dx, period);

                    uint32_t h = Hash(c, m_worleySeed);
                    float jx = HashToUnit01(h);
                    float jy = HashToUnit01(h * 0x9E3779B1u);
                    float jz = HashToUnit01(h * 0xBB67AE85u);

                    GridPosf feature(
                        (float)c.x + jx,
                        (float)c.y + jy,
                        (float)c.z + jz
                    );

                    GridPosf d = feature - pos;

                    if (d.x > 0.5f * period) d.x -= period;
                    if (d.x < -0.5f * period) d.x += period;
                    if (d.y > 0.5f * period) d.y -= period;
                    if (d.y < -0.5f * period) d.y += period;
                    if (d.z > 0.5f * period) d.z -= period;
                    if (d.z < -0.5f * period) d.z += period;

                    float dist = d.Dot(d);
                    if (dist < minDist)
                        minDist = dist;
                }
            }
        }
        return 1.0f - minDist;
    }

#else

    float CloudNoise::WorleyNoise(const Vector3f& uv, float freq) {
        vec3 id = glm::floor(uv);
        vec3 p = glm::fract(vec3(uv));

        float minDist = 10000.0f;

        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                for (int z = -1; z <= 1; ++z) {
                    vec3 offset(
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(z)
                    );

                    vec3 cell = glm::mod(id + offset, vec3(freq));
                    vec3 h = Hash33(cell) * 0.5f + vec3(0.5f);
                    h += offset;

                    vec3 d = p - h;
                    float dist = glm::dot(d, d);

                    if (dist < minDist) {
                        minDist = dist;
                    }
                }
            }
        }
        return 1.0f - minDist;
    }

#endif

    float CloudNoise::PerlinFBM(Vector3f p, float freq, int octaves) {
        // alternativ: gain = 0.5f; octaves = 6; freq = 16;
        // alternativ: gain = std::exp2(-0.5f); octaves = 7; freq = 8;
        float gain = 0.75f; 
        float amp = 1.0f;
        float noise = 0.0f;

        for (int i = 0; i < octaves; ++i) {
#if NOISE_TYPE
#   if 1
            m_perlin.Setup((int)freq, m_perlinSeed + i * 0x9E3779B9u);
            noise += amp * m_perlin.Compute(p * freq);
#   else
            noise += amp * PeriodicSimplexAshima(p * freq, int(freq)); // , m_perlinSeed);
#   endif
#else
            noise += amp * GradientNoise(p * freq, freq);
#endif
            freq *= 2.0f;
            amp *= gain;
        }
        return noise;
    }


    float CloudNoise::WorleyFBM(Vector3f p, float freq) {
#if NOISE_TYPE
        float n = Worley(p * freq, int(freq), m_worleySeed) * 0.625f +
                  Worley(p * freq * 2.0f, int(freq * 2.0f), m_worleySeed) * 0.25f +
                  Worley(p * freq * 4.0f, int(freq * 4.0f), m_worleySeed) * 0.125f;
#else
        float n = WorleyNoise(p * freq, freq) * 0.625f +
                  WorleyNoise(p * freq * 2.0f, freq * 2.0f) * 0.25f +
                  WorleyNoise(p * freq * 4.0f, freq * 4.0f) * 0.125f;
#endif
        return std::max(0.f, 1.1f * n - .1f);
    }

    // Entspricht mainImage: erzeugt RGBA-Rauschwert für ein Pixel
    RGBAColor CloudNoise::Compute(Vector3f p) {
        float baseFreq = 4.0f;
#if 0
        // this severely distorts the noise distribution
        float pfbm = 0.5f * (1.0f + PerlinFBM(p, baseFreq * 8, 7));
        pfbm = /*std::fabs*/(pfbm * 2.0f - 1.0f);
#endif
        RGBAColor color{
            0.5f * (1.0f + PerlinFBM(p, 4.0f, 7)),
            WorleyFBM(p, baseFreq),
            WorleyFBM(p, baseFreq * 2.0f),
            WorleyFBM(p, baseFreq * 4.0f),
        };
#if 0
        // remap compresses the noise values even more
        color.r = Remap(color.r, 0.0f, 1.0f, color.g, 1.0f);
#endif

        return color;
    }
};

// =================================================================================================
