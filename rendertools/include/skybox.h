#pragma once

#include "matrix.hpp"
#include "basesingleton.hpp"
#include "texture.h"
#include "mesh.h"
#include "noisetexture.h"
#include "base_shaderhandler.h"
#include "texturehandler.h"

// =================================================================================================

class Skybox
	: public BaseSingleton<Skybox>
{
private:
	Cubemap* m_skyTextures[3][3] = { { nullptr, nullptr, nullptr }, { nullptr, nullptr, nullptr }, { nullptr, nullptr, nullptr } };
	CloudNoiseTexture* m_noiseTexture{ nullptr };
	Mesh* m_skybox{ nullptr };
	int32_t	m_activationTime{ -1 };

public:
	Skybox() = default;

	~Skybox() = default;

	bool Setup(const String& textureFolder, CloudNoiseTexture* noiseTexture = nullptr);

	bool Render(int32_t skyType, Matrix4f& view, Vector3f lightDirection, float brightness, int32_t currentTime);

	inline bool IsAvailable(void) const noexcept {
		return (m_skybox != nullptr);
	}

	inline void FadeIn(uint32_t t) noexcept {
		m_activationTime = t;
	}

	inline bool HasNightSky(int32_t i) noexcept {
		return (i == 0) ? false : m_skyTextures[i-1][0] != nullptr;
	}

private:
	Cubemap* LoadTextures(const String& textureFolder, const String& baseName, const String& type, const String& size);

	Shader* LoadShader(Matrix4f& view, Vector3f lightDirection, float brightness, float alpha, int32_t currentTime);

	int MaxTextureSize(void);
};

#define skybox Skybox::Instance()

// =================================================================================================
