#pragma once

// =================================================================================================

namespace Noise
{
	float Perlin(float x, float y, float z); // 3D perlin noise

	float ImprovedNoise(float x, float y, float z, const std::vector<int>& perm, int period);

	void BuildPermutation(std::vector<int>& perm, int period, uint32_t seed);

	float SimplexPerlin(float x, float y, float z);

	float SimplexAshima(float x, float y, float z);

};

// =================================================================================================

