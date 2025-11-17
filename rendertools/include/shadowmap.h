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

	bool Update(Vector3f lightDirection, float lightOffset);

	int IsAvailable(void) noexcept {
		return (m_status > 0);
	}

	Matrix4f& ShadowTransform(void) noexcept {
		return m_shadowTransform;
	}

	inline bool StartRender(void) noexcept {
		if (not IsAvailable())
			return false;
		baseRenderer.SelectMatrixStack(m_matrixIndex);
		baseRenderer.StartShadowPass();
		m_map->Enable();
		return true;
	}

	inline bool StopRender(void) noexcept {
		if (not IsAvailable())
			return false;
		baseRenderer.SelectMatrixStack(0);
		m_map->Disable();
		return true;
	}

private:
	bool CreateMap(Vector2f frustumSize);
};

#define shadowMap	ShadowMap::Instance()

// =================================================================================================


