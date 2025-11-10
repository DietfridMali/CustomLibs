
#include <math.h>
#include <stdint.h>

#include "perlin.h"

// =================================================================================================

struct grad3 { float x, y, z; };

// 5th degree smoothstep (better smoothing)
static inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// linear interpolation
static inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

static inline uint32_t Hash(int x, int y, int z) {
    uint32_t h = 
          (uint32_t)x * 374761393u
        + (uint32_t)y * 668265263u
        + (uint32_t)z * 2147483647u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

static inline grad3 RandomGradient(int ix, int iy, int iz) {
    static const grad3 gradLUT[12] = {
        { 1, 1, 0},{-1, 1, 0},{ 1,-1, 0},{-1,-1, 0},
        { 1, 0, 1},{-1, 0, 1},{ 1, 0,-1},{-1, 0,-1},
        { 0, 1, 1},{ 0,-1, 1},{ 0, 1,-1},{ 0,-1,-1}
    };

    uint32_t h = Hash(ix, iy, iz);
    return gradLUT[h % 12];
}

static inline float DotGridGradient(int ix, int iy, int iz, float x, float y, float z) {
    grad3 g = RandomGradient(ix, iy, iz);
    float dx = x - (float)ix;
    float dy = y - (float)iy;
    float dz = z - (float)iz;
    return dx * g.x + dy * g.y + dz * g.z;
}

// 3D Perlin, nicht periodisch, ~[-1,1]
float PerlinNoise(float x, float y, float z) {
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

    float s = fade(x - (float)x0);
    float nx00 = lerp(n000, n100, s);
    float nx10 = lerp(n010, n110, s);
    float nx01 = lerp(n001, n101, s);
    float nx11 = lerp(n011, n111, s);

    s = fade(y - (float)y0);
    float nxy0 = lerp(nx00, nx10, s);
    float nxy1 = lerp(nx01, nx11, s);

    return lerp(nxy0, nxy1, fade(z - (float)z0));
}

// =================================================================================================
