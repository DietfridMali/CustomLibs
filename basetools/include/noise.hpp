#pragma once

#include <cstdint>
#include <cmath>
#include "vector.hpp"

// =================================================================================================
// 1D gradient (Perlin) noise. The value is exactly 0 at every integer coordinate, so sampling a
// whole [0, n] span with integer n yields a curve that begins and ends at 0. That anchors a
// lightning bolt's sideways displacement at its two endpoints without an extra window function.

class Noise {
public:
    static float Perlin1D(float x, uint32_t seed) {
        int32_t i0 = int32_t(std::floor(x));
        float f = x - float(i0);
        float g0 = Gradient(i0, seed);
        float g1 = Gradient(i0 + 1, seed);
        float u = Fade(f);
        float n0 = g0 * f;
        float n1 = g1 * (f - 1.0f);
        return (n0 + (n1 - n0) * u) * 2.0f;
    }

    static Vector3f Perlin3D(float x, uint32_t seed) {
        return Vector3f(Perlin1D(x, seed), Perlin1D(x, seed ^ 0x9e3779b9u), Perlin1D(x, seed ^ 0x85ebca6bu));
    }

private:
    static uint32_t Hash(uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    static float Gradient(int32_t i, uint32_t seed) {
        return float(Hash(uint32_t(i) ^ seed) & 0xffffu) * (1.0f / 32767.5f) - 1.0f;
    }

    static float Fade(float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
};

// =================================================================================================
