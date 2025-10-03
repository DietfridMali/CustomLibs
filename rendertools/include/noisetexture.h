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
	ManagedArray<float>	m_data;

	bool Allocate(int edgeSize);

	void ComputeNoise(int edgeSize, int yPeriod, int xPeriod, int octaves, uint32_t seed);

};

// =================================================================================================

class NoiseTexture3D
	: public Texture
{
public:
	virtual void Deploy(int bufferIndex = 0) override;

	virtual void SetParams(bool enforce = false) override;

	bool Create(int edgeSize);

private:
	int m_edgeSize;

	ManagedArray<uint8_t>	m_data;

	bool Allocate(int edgeSize);

	void ComputeNoise(void);

};

// =================================================================================================

