#pragma once

// =================================================================================================

namespace Perlin
{
	float Noise(float x, float y, float z); // 3D perlin noise

	float ImprovedNoise(float x, float y, float z, const std::vector<int>& perm, int period);

	void BuildPermutation(std::vector<int>& perm, int period, uint32_t seed);
};

// =================================================================================================

