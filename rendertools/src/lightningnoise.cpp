#include <cmath>
#include "lightningnoise.h"

// =================================================================================================
// 1D gradient (Perlin) noise, value exactly 0 at every integer coordinate.

uint32_t LightningNoise::Hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}


float LightningNoise::Gradient(int32_t i, uint32_t seed) {
    return float(Hash(uint32_t(i) ^ seed) & 0xffffu) * (1.0f / 32767.5f) - 1.0f;
}


float LightningNoise::Fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}


float LightningNoise::Perlin1D(float x, uint32_t seed) {
    int32_t i0 = int32_t(std::floor(x));
    float f = x - float(i0);
    float g0 = Gradient(i0, seed);
    float g1 = Gradient(i0 + 1, seed);
    float u = Fade(f);
    float n0 = g0 * f;
    float n1 = g1 * (f - 1.0f);
    return (n0 + (n1 - n0) * u) * 2.0f;
}


Vector3f LightningNoise::Perlin3D(float x, uint32_t seed) {
    return Vector3f(Perlin1D(x, seed), Perlin1D(x, seed ^ 0x9e3779b9u), Perlin1D(x, seed ^ 0x85ebca6bu));
}

// =================================================================================================
// fBm: sum of Perlin octaves, normalized to ~[-1, 1]. The per-octave phase offset (o * golden ratio)
// keeps the octaves' integer zero crossings from coinciding.

float LightningNoise::Fbm1D(float x, uint32_t seed, int32_t octaves, float gain, float lacunarity) {
    if (octaves < 1)
        octaves = 1;
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int32_t o = 0; o < octaves; o++) {
        sum += Perlin1D(x * freq + float(o) * 1.6180339887f, seed + uint32_t(o) * 0x9e3779b9u) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return (norm > 1e-6f) ? sum / norm : sum;
}


Vector3f LightningNoise::Fbm3D(float x, uint32_t seed, int32_t octaves, float gain, float lacunarity) {
    if (octaves < 1)
        octaves = 1;
    Vector3f sum(0.0f, 0.0f, 0.0f);
    float amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int32_t o = 0; o < octaves; o++) {
        sum += Perlin3D(x * freq + float(o) * 1.6180339887f, seed + uint32_t(o) * 0x9e3779b9u) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return (norm > 1e-6f) ? sum * (1.0f / norm) : sum;
}

// =================================================================================================
