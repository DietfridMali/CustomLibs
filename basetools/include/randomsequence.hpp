#pragma once

#include <random>
#include <cstdint>

// =================================================================================================

class RandomSequence {
private:
	std::mt19937	m_engine;

public:
	explicit RandomSequence(uint32_t seed = 0)
		: m_engine(seed)
	{
	}

	void Seed(uint32_t seed) {
		m_engine.seed(seed);
	}

	uint32_t Next(void) {
		return uint32_t(m_engine());
	}

	float Float(void) {
		return float(Next() >> 8) * (1.0f / 16777216.0f);
	}

	float Float(float scale) {
		return Float() * scale;
	}

	float Float(float minValue, float maxValue) {
		return minValue + Float() * (maxValue - minValue);
	}

	int Int(int maxValue) {
		return int(Next() % uint32_t(maxValue));
	}

	int Int(int minValue, int maxValue) {
		return Int(maxValue - minValue + 1) + minValue;
	}
};

// =================================================================================================
