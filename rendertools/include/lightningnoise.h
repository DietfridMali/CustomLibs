#pragma once

#include <cstdint>
#include "vector.hpp"

// =================================================================================================
// Fractal (fBm) value noise for procedural lightning. Self-contained (its own 1D gradient Perlin,
// kept here in rendertools rather than in basetools): a bolt samples this along its length to get a
// coarse overall swing plus progressively finer zig-zags.
//
// The 1D Perlin is exactly 0 at every integer coordinate. Fbm* sum `octaves` Perlin octaves
// (per-octave frequency *= lacunarity, amplitude *= gain) and normalize the result to ~[-1, 1].
// A per-octave phase offset (golden-ratio spacing) keeps the octaves' zero crossings from lining up
// -- otherwise a bolt would return exactly to its axis at regular intervals ("beads on a string").
//
// The 2D variants (Fbm2D / Fbm2Dv3) take a second axis meant as an independent "time" axis for animation:
// advancing y morphs the bolt in place, whereas adding time onto the length axis would slide the whole
// shape sideways (a visible drift). y is left unscaled per octave (no golden-ratio offset) since a bolt
// is never sampled at integer time boundaries the way it is along its length.

class LightningNoise {
public:
    static float    Perlin1D(float x, uint32_t seed);
    static Vector3f Perlin3D(float x, uint32_t seed);
    static float    Fbm1D(float x, uint32_t seed, int32_t octaves, float gain, float lacunarity);
    static Vector3f Fbm3D(float x, uint32_t seed, int32_t octaves, float gain, float lacunarity);

    static float    Perlin2D(float x, float y, uint32_t seed);
    static float    Fbm2D(float x, float y, uint32_t seed, int32_t octaves, float gain, float lacunarity);
    static Vector3f Fbm2Dv3(float x, float y, uint32_t seed, int32_t octaves, float gain, float lacunarity);

private:
    static uint32_t Hash(uint32_t x);
    static float    Gradient(int32_t i, uint32_t seed);
    static float    GradDot2(int32_t ix, int32_t iy, float dx, float dy, uint32_t seed);
    static float    Fade(float t);
};

// =================================================================================================
