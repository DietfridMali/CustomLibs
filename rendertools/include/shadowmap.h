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
	SimpleArray<Vector3f, 8>	m_frustumCorners;	// index 8: center
	MatrixStack*				m_matrices{ nullptr };
	Matrix4f					m_shadowTransform;
	int							m_matrixIndex{ -1 };
	FBO*						m_map{ nullptr };
	int							m_status{ 0 };

public:
	void Setup(void);

	bool Update(Vector3f center, Vector3f lightDirection, float lightOffset, Vector3f worldMin, Vector3f worldMax);

	void Advance(void);

	int IsAvailable(void) noexcept {
		return (m_status >= 0);
	}

	int IsReady(void) noexcept {
		return (m_status > 0);
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
		baseRenderer.SelectMatrixStack(m_matrixIndex);
		baseRenderer.PushViewport();
		m_map->SetViewport();
	}

	inline void DisableCamera(void) noexcept {
		baseRenderer.SelectMatrixStack(0);
		baseRenderer.PopViewport();
	}

	void Destroy(void) noexcept;

private:
	bool CreateMap(Vector2f frustumSize);

	void Stabilize(float shadowMapSize);
};

#define shadowMap	ShadowMap::Instance()

// =================================================================================================


