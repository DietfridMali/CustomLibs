#pragma once
#include "texture.h"

// =================================================================================================
// Load cubemap textures from file and generate an OpenGL cubemap

class Cubemap
	: public Texture {
public:
	Cubemap()
		: Texture(0, TextureType::CubeMap)
	{}

	virtual void SetParams(bool forceUpdate = false) override;

	virtual bool Deploy(int bufferIndex = 0);
};

// =================================================================================================
