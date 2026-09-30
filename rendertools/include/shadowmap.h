#pragma once

#include "array.hpp"
#include "matrix.hpp"
#include "basesingleton.hpp"
#include "gfxrenderer.h"

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
	RenderTarget*				m_map{ nullptr };
	float						m_maxLightRadius{ 15.0f };
	int							m_status{ 0 };
	bool						m_renderShadows{ true };
	bool						m_applyShadows{ false };
	Vector3f					m_lightPosition{ Vector3f::ZERO };

public:
	bool Setup(void);

	bool Update(Vector3f center, Vector3f lightDirection, float lightOffset, Vector3f worldMin, Vector3f worldMax);

	void UpdateTransformation(void);

	int IsAvailable(void) noexcept {   // do we have a shadow-map RT at all (mandatory wherever the shadow texture must be bound)
		return m_status >= 0;
	}

	int IsApplicable(void) noexcept {  // should cast shadows actually be rendered / applied this frame
		return IsAvailable() and m_applyShadows and m_renderShadows;
	}

	int IsReady(void) noexcept {
		return m_renderShadows and m_applyShadows and (m_status > 0);
	}

	Matrix4f& GetTransformation(bool forClipSpace = true) noexcept {
		return forClipSpace ? m_modelViewTransform : m_lightTransform;
	}

	bool StartRender(void) noexcept;

	bool StopRender(void) noexcept;

	inline void MakeReadable(void) noexcept {
		if (m_map and m_map->Activate({ .bufferIndex = 0, .drawBufferGroup = RenderTarget::dbDepth }))
			m_map->Deactivate();
	}

	inline RenderTarget* GetMap(void) noexcept {
		return m_map;
	}

	inline Texture* RenderTexture(void) noexcept {
		return m_map ? m_map->GetAsTexture({}) : nullptr;
	}

	inline Texture* ShadowTexture(void) noexcept {
		return m_map ? m_map->GetDepthAsShadowTexture() : nullptr;
	}

	inline void ActivateCamera(void) noexcept {
		baseRenderer.PushViewport();
		m_map->SetViewport();
	}

	inline void DeactivateCamera(void) noexcept {
		baseRenderer.PopViewport();
	}

	void Destroy(void) noexcept;

	inline void SetRenderShadows(bool renderShadows) noexcept {
		m_renderShadows = renderShadows;
	}

	inline void ToggleRenderShadows(void) noexcept {
		m_renderShadows = not m_renderShadows;
	}

	inline bool RenderShadows(void) noexcept {
		return m_renderShadows;
	}

	inline void SetApplyShadows(bool applyShadows) noexcept {
		m_applyShadows = applyShadows;
	}

	inline bool ApplyShadows(void) noexcept {
		return m_applyShadows;
	}

	inline const Vector3f& LightPosition() const noexcept {
		return m_lightPosition;
	}

	// Half the width of the focused light frustum in world units (frustumWidth == 2 * MaxLightRadius).
	// Shaders take this as the "shadowCoverage" uniform; it must not be duplicated as a shader-side
	// constant, because CreateMap() may shrink it when the shadow map has to be downsized.
	inline float MaxLightRadius() const noexcept {
		return m_maxLightRadius;
	}

	inline Vector3f LightDirection(Vector3f p) noexcept {
		return (m_lightPosition - p).Normalize();
	}

private:
	bool CreateMap(Vector2f frustumSize);

	void Stabilize(float shadowMapSize);

	void CreateViewerAlignedTransformation(Vector3f center, const Vector3f& lightDirection, float lightDistance, const Vector3f& worldMin, const Vector3f& worldMax);

	void CreatePerspectiveTransformation(const Vector3f& center, const Vector3f& lightDirection, float lightDistance, float worldRadius);

	void CreateOrthoTransformation(const Vector3f& center, const Vector3f& lightDirection, float lightOffset, const Vector3f& worldSize, const Vector3f& worldMin, const Vector3f& worldMax);

	void CreateLightTransformation(const Matrix4f& lightView, const Matrix4f& lightProj);
};

#define shadowMap	ShadowMap::Instance()

// =================================================================================================

class ShadowAtlas
{
public:
	struct PointLightFrustum {
		Matrix4f	view;
		Matrix4f	projection;
		Matrix4f	viewProjection;
		float		tanHalfFov{ 0.0f };
		float		zNear{ 0.0f };
		float		zFar{ 0.0f };
	};

private:
	RenderTarget*	m_map{ nullptr };
	int				m_size{ 0 };
	int				m_minTileSize{ 0 };
	int				m_maxTileSize{ 0 };
	int				m_tileSize{ 0 };
	int				m_tilesPerRow{ 0 };

public:
	~ShadowAtlas() {
		Destroy();
	}

	bool Create(int size, int maxTileSize, int minTileSize);

	void Destroy(void) noexcept;

	int Layout(int tileCount) noexcept;

	Viewport TileViewport(int tile) const noexcept;

	static bool PointLightTransformation(const Vector3f& lightPosition, const Vector3f& center, float radius, float zFar, float margin, PointLightFrustum& frustum);

	inline bool IsAvailable(void) noexcept {
		return (m_map != nullptr) and m_map->IsAvailable();
	}

	inline RenderTarget* GetMap(void) noexcept {
		return m_map;
	}

	inline Texture* ShadowTexture(void) noexcept {
		return m_map ? m_map->GetDepthAsShadowTexture() : nullptr;
	}

	inline int Size(void) const noexcept {
		return m_size;
	}

	inline int TileSize(void) const noexcept {
		return m_tileSize;
	}

	inline int TileCount(void) const noexcept {
		return m_tilesPerRow * m_tilesPerRow;
	}
};

// =================================================================================================


