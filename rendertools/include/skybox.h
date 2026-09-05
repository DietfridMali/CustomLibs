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
	Texture* m_blueNoise{ nullptr };
	Mesh* m_skybox{ nullptr };
	int32_t	m_activationTime{ -1 };

public:
	Skybox() = default;

	~Skybox() = default;

	// maxTextureSize caps the edge length of the cube map faces that will be looked for, in pixels.
	// 0 asks for the largest set the GPU can hold, which is what Paintjob Rampage wants; an application
	// that ships only one size says so and gets that one (d2x-xl: 2048). It is a CAP, not a demand -
	// a GPU that cannot hold that much still gets the next size down.
	bool Setup(const String& textureFolder, CloudNoiseTexture* noiseTexture = nullptr, Texture* blueNoise = nullptr, int maxTextureSize = 0);

	// Releases the cube maps and the cube, so that Setup () can run again. Needed by any application
	// that can lose its textures while running (d2x-xl throws all of them away on a video mode change);
	// without it a second Setup () would overwrite the pointers and leak both the textures and the mesh.
	// The noise textures are NOT touched - they belong to whoever passed them in.
	void Destroy(void);

	bool Render(int32_t skyType, Matrix4f& view, Vector3f lightDirection, float brightness, int32_t currentTime);

	inline bool IsAvailable(void) const noexcept {
		return (m_skybox != nullptr);
	}

	inline void FadeIn(uint32_t t) noexcept {
		m_activationTime = t;
	}

	inline bool HasNightSky(int32_t i) noexcept {
		return (i == 0) ? false : m_skyTextures[(i - 1) % 2][0] != nullptr; // i == 2 --> sky #0 with black hole
	}

private:
	Cubemap* LoadTextures(const String& textureFolder, const String& baseName, const String& type, const String& size);

	Shader* LoadShader(Matrix4f& view, Vector3f lightDirection, float brightness, float alpha, int32_t currentTime);

	Shader* LoadBlackholeShader(Matrix4f& view, Vector3f lightDirection, float brightness, float alpha, int32_t currentTime);

	int MaxTextureSize(int maxTextureSize);
};

#define skybox Skybox::Instance()

// =================================================================================================
