#pragma once

#include "array.hpp"
#include "matrix.hpp"
#include "basesingleton.hpp"
#include "base_renderer.h"

// =================================================================================================

class ShadowMap
	: public BaseSingleton<ShadowMap>
{
private:
	Vector4f					m_ndcCorners[8] {
									{-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f, 1.0f},
									{-1.0f, -1.0f,  1.0f, 1.0f}, {1.0f, -1.0f,  1.0f, 1.0f}, {1.0f, 1.0f,  1.0f, 1.0f}, {-1.0f, 1.0f,  1.0f, 1.0f}
	};
	Matrix4f					m_lightTransform;
	Matrix4f					m_modelViewTransform;
	FBO*						m_map{ nullptr };
	int							m_status{ 0 };
	bool						m_renderShadows{ true };
	bool						m_applyShadows{ false };
	Vector3f					m_lightPosition{ Vector3f::ZERO };

public:
	bool Setup(void);

	bool Update(Vector3f center, Vector3f lightDirection, float lightOffset, Vector3f worldMin, Vector3f worldMax);

	void UpdateTransformation(void);

	int IsAvailable(void) noexcept {
		return m_renderShadows and m_applyShadows and (m_status >= 0);
	}

	int IsReady(void) noexcept {
		return m_renderShadows and m_applyShadows and (m_status > 0);
	}

	Matrix4f& ShadowTransform(void) noexcept {
		return m_modelViewTransform;
	}

	bool StartRender(void) noexcept;

	bool StopRender(void) noexcept;

	inline FBO* GetMap(void) noexcept {
		return m_map;
	}

	inline Texture* RenderTexture(void) noexcept {
		return m_map ? m_map->GetRenderTexture({}) : nullptr;
	}

	inline Texture* ShadowTexture(void) noexcept {
		return m_map ? m_map->GetDepthTexture() : nullptr;
	}

	inline void EnableCamera(void) noexcept {
		baseRenderer.PushViewport();
		m_map->SetViewport();
	}

	inline void DisableCamera(void) noexcept {
		baseRenderer.PopViewport();
	}

	void Destroy(void) noexcept;

	inline void SetRenderShadows(bool renderShadows) noexcept {
		m_renderShadows = renderShadows;
	}

	inline bool RenderShadows(void) noexcept {
		return m_renderShadows;
	}

	inline void ApplyShadows(bool applyShadows) noexcept {
		m_applyShadows = applyShadows;
	}

	inline const Vector3f& LightPosition() const noexcept {
		return m_lightPosition;
	}

	inline Vector3f LightDirection(Vector3f p) noexcept {
		return (m_lightPosition - p).Normalize();
	}

private:
	bool CreateMap(Vector2f frustumSize);

	void Stabilize(float shadowMapSize);

	void CreatePerspectiveTransformation(const Vector3f& center, const Vector3f& lightDirection, float lightDistance, float worldRadius);

	void CreateOrthoTransformation(const Vector3f& center, const Vector3f& lightDirection, const Vector3f& worldSize, const Vector3f& worldMin, const Vector3f& worldMax);

	void CreateLightTransformation(const Matrix4f& lightView, const Matrix4f& lightProj);
};

#define shadowMap	ShadowMap::Instance()

// =================================================================================================


