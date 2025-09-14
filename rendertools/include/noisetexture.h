#pragma once

#include "texture.h"

// =================================================================================================

class NoiseTexture
	: public Texture
{
public:
	virtual void Deploy(int bufferIndex = 0) override;

	virtual void SetParams(bool enforce = false) override;

	bool Create(int edgeSize, int periodX, int periodY, int octaves = 3, uint32_t seed = 1);

private:
	bool Allocate(int size);

	void ComputeNoise(int edgeSize, int yPeriod, int xPeriod, int octaves, uint32_t seed);

};

// =================================================================================================

