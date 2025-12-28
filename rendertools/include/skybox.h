#pragma once

#include "matrix.hpp"
#include "basesingleton.hpp"
#include "texture.h"
#include "mesh.h"
#include "base_shaderhandler.h"
#include "texturehandler.h"

// =================================================================================================

class Skybox 
	: public BaseSingleton<Skybox>
{
private:
	Cubemap* m_texture = nullptr;
	Mesh*	 m_skybox = nullptr;

public:
	Skybox() = default;

	~Skybox() = default;

	bool Setup(const String& textureFolder);

	void Render(Matrix4f& view);

private:
	bool LoadTextures(const String& textureFolder);

	Shader* LoadShader(Matrix4f& view);
};

#define skybox Skybox::Instance()

// =================================================================================================
