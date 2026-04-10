
#include <math.h>
#include <stdint.h>
#include <vector>

#include "perlin.h"

// =================================================================================================

namespace Perlin {

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
    };

    // 3D Perlin, nicht periodisch, ~[-1,1]
    float Noise(float x, float y, float z) {
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

    float ImprovedNoise(float x, float y, float z, const std::vector<int>& perm, int period) {
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

};

// =================================================================================================
