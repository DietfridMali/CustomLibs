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
	Cubemap* m_skyTextures[3] = { nullptr, nullptr, nullptr };
	Mesh*	 m_skybox = nullptr;

public:
	Skybox() = default;

	~Skybox() = default;

	bool Setup(const String& textureFolder);

	void Render(Matrix4f& view, Vector3f lightDirection, float brightness);

private:
	Cubemap* LoadTextures(const String& textureFolder, const String& type, const String& size);

	Shader* LoadShader(Matrix4f& view, Vector3f lightDirection, float brightness);

	int MaxTextureSize(void);
};

#define skybox Skybox::Instance()

// =================================================================================================
