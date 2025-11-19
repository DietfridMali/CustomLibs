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
	Matrix4f					m_shadowTransform;
	FBO*						m_map{ nullptr };
	int							m_status{ 0 };
	bool						m_renderShadows{ true };
	bool						m_applyShadows{ false };

public:
	void Setup(void);

	bool Update(Vector3f center, Vector3f lightDirection, float lightOffset, Vector3f worldMin, Vector3f worldMax);

	int IsAvailable(void) noexcept {
		return m_renderShadows and m_applyShadows and (m_status >= 0);
	}

	int IsReady(void) noexcept {
		return m_renderShadows and m_applyShadows and (m_status > 0);
	}

	Matrix4f& ShadowTransform(void) noexcept {
		return m_shadowTransform;
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

private:
	bool CreateMap(Vector2f frustumSize);

	void Stabilize(float shadowMapSize);
};

#define shadowMap	ShadowMap::Instance()

// =================================================================================================


