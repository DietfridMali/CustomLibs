#include "shadowmap.h"

#define APPLY_POLYGON_OFFSET 1

// =================================================================================================

bool ShadowMap::Setup(void) {
	if (CreateMap(Vector2f::ZERO)) {
		m_status = 1;
		return true;
	}
	m_status = -1;
	return false;
}


bool ShadowMap::CreateMap(Vector2f frustumSize) {
	m_status = -1;
#if !DEMO
	if (not (m_map = new RenderTarget()))
		return false;
	// ShadowMap-Format ist D32_FLOAT (4 Byte/Pixel, single-channel, kein Stencil) — siehe
	// rendertarget.cpp/resource_view.h. Start bei 8K (industry-typische ShadowMap-Aufloesung),
	// halbieren bei Fehlschlag bis 1024. Cap zusaetzlich gegen die Hardware-Allocation-Grenze
	// fuer 4-Byte-Pixel-Formate, falls die GPU weniger als 8K verkraftet.
	constexpr int ShadowDepthBytesPerPixel = 4;
	const int maxSize = gfxStates.MaxTextureSize(ShadowDepthBytesPerPixel);
	int startSize = std::min<int>(maxSize, 8192);
	for (int size = startSize; size >= 1024; size /= 2) {
		if (m_map->Create(size, size, 1, { .name = "shadowmap", .colorBufferCount = 0, .depthBufferCount = 1, .vertexBufferCount = 0, .hasMRTs = false })) {
			m_status = 1;
			return true;
		}
		//m_maxLightRadius *= 0.9f;
	}
#endif
	return false;
}


void ShadowMap::Destroy(void) noexcept {
	if (m_map) {
		delete m_map;
		m_map = nullptr;
		m_status = 0;
	}
}


bool ShadowMap::StartRender(void) noexcept {
	if (not IsReady())
		return false;
	baseRenderer.StartShadowPass();
	m_map->Activate({ .bufferIndex = 0, .drawBufferGroup = RenderTarget::dbDepth });
	// DX12: depth clear is handled by RenderTarget::Enable / OMSetRenderTargets + ClearDepthStencilView
	ActivateCamera();
	gfxStates.SetDepthTest(1);
	gfxStates.SetDepthWrite(1);
	gfxStates.SetFaceCulling(1);
	gfxStates.CullFace(GfxOperations::CullFace::Front);
#if APPLY_POLYGON_OFFSET
	gfxStates.SetPolygonOffset(1.0f, 1.0f);
#endif
	return true;
}


bool ShadowMap::StopRender(void) noexcept {
	if (not IsReady())
		return false;
	DeactivateCamera();
	gfxStates.CullFace(GfxOperations::CullFace::Back);
#if APPLY_POLYGON_OFFSET
	gfxStates.SetPolygonOffset(0.0f, 0.0f);
#endif
	m_map->Deactivate();
	return true;
}


void ShadowMap::Stabilize(float shadowMapSize)
{
	// NOTE: inert with the active perspective path -- UpdateTransformation() overwrites m_modelViewTransform
	// right after this. The working texel-snap belongs to the viewer-focused ORTHO path (#if 0'd in
	// CreateViewerAlignedTransformation); it must snap m_lightTransform and is ortho-only.
	Vector4f shadowOrigin = m_modelViewTransform * Vector4f(0.0f, 0.0f, 0.0f, 1.0f);
	shadowOrigin *= shadowMapSize * 0.5f;
	Vector2f roundedOrigin = Vector2f::Round(Vector2f(shadowOrigin.x, shadowOrigin.y));
	Vector2f offset = (roundedOrigin - Vector2f(shadowOrigin.x, shadowOrigin.y)) * (2.0f / shadowMapSize);
	m_modelViewTransform.T().x += offset.x;
	m_modelViewTransform.T().y += offset.y;
}


void ShadowMap::CreateLightTransformation(const Matrix4f& lightView, const Matrix4f& lightProj) {
	m_lightTransform = lightProj;
	m_lightTransform *= lightView;
	Stabilize(float(m_map->GetWidth(true)));
	UpdateTransformation();
}


void ShadowMap::UpdateTransformation(void) { // needs to be called whenever mModelView for a shader using shadow mapping changes (e.g. for moving geometry)
	if (IsReady()) {
		m_modelViewTransform = m_lightTransform;
#if 1
		m_modelViewTransform *= baseRenderer.Matrices(0)->ModelView().Inverse();
#endif
	}
}


// Forward view: viewer-aligned PERSPECTIVE frustum (active). The viewer-focused ORTHO variant below is kept
// under #if 0 -- no perceptible benefit over perspective, only crawling stepped edges (even at 4K): its
// texel-snap (Stabilize) damps only translation, not the sun's slow rotation, and ortho spreads texels
// uniformly so the near field stays coarse. Viewer sits near the rear edge (centre shifted 0.8*radius fwd).
void ShadowMap::CreateViewerAlignedTransformation(Vector3f center, const Vector3f& lightDirection, float lightDistance, const Vector3f& worldMin, const Vector3f& worldMax) {
	Matrix4f lightView, lightProj;

#if 0   // viewer-focused ORTHO frustum (disabled). Re-enabling also needs the m_lightTransform texel-snap
        // in Stabilize() (ortho-only). frustumWidth == 2*coverage matches shadowCoverage in the shaders.
	float coverage = m_maxLightRadius;
	if (lightDistance <= 0.0f)
		lightDistance = 100.0f * coverage;
	Vector3f viewDir = baseRenderer.Matrices(0)->ModelView().Inverse() * Vector3f(0.0f, 0.0f, -1.0f);
	viewDir.Normalize();
	center += viewDir * coverage * 0.8f;   // viewer near rear edge
	m_lightPosition = center + lightDirection * lightDistance;
	lightView.LookAt(m_lightPosition, center, Vector3f(0.0f, 1.0f, 0.0f));
	Vector4f corners[8] = {
		{ center.X() - coverage, worldMin.Y(), center.Z() - coverage, 1.0f },
		{ center.X() + coverage, worldMin.Y(), center.Z() - coverage, 1.0f },
		{ center.X() - coverage, worldMax.Y(), center.Z() - coverage, 1.0f },
		{ center.X() + coverage, worldMax.Y(), center.Z() - coverage, 1.0f },
		{ center.X() - coverage, worldMin.Y(), center.Z() + coverage, 1.0f },
		{ center.X() + coverage, worldMin.Y(), center.Z() + coverage, 1.0f },
		{ center.X() - coverage, worldMax.Y(), center.Z() + coverage, 1.0f },
		{ center.X() + coverage, worldMax.Y(), center.Z() + coverage, 1.0f }
	};
	Vector4f vMin{ FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
	Vector4f vMax{ -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for (int i = 0; i < 8; i++) {
		Vector4f v = lightView * corners[i];
		vMin.Minimize(v);
		vMax.Maximize(v);
	}
	Projector projector;
	lightProj = projector.ComputeOrthoProjection(vMin.x, vMax.x, vMin.y, vMax.y, -vMax.z, -vMin.z);
#else   // viewer-aligned PERSPECTIVE frustum: worldRadius-sized cone around the area in front of the viewer,
        // centre shifted forward so the viewer sits near the rear edge. Near field gets more density than ortho.
	float worldRadius = std::min(Vector3f::Abs(worldMax - worldMin).Length() * 0.5f, m_maxLightRadius);
	if (lightDistance <= 0.0f)
		lightDistance = 100.0f * worldRadius;
	Vector3f viewDir = baseRenderer.Matrices(0)->ModelView().Inverse() * Vector3f(0.0f, 0.0f, -1.0f);
	viewDir.Normalize();
	center += viewDir * worldRadius * 0.8f;
	Vector3f f = -lightDirection;
	float dotFV = f.Dot(viewDir);
	Vector3f s = viewDir - f * dotFV;      // view-direction component perpendicular to the light
	s.Normalize();
	Vector3f upParam = s.Cross(f);
	upParam.Normalize();
	m_lightPosition = center + lightDirection * lightDistance;
	lightView.LookAt(m_lightPosition, center, upParam);
	float halfFov = std::atan(worldRadius / lightDistance);
	float zNear = std::max(0.01f, lightDistance - worldRadius);
	float zFar = lightDistance + worldRadius;
	Projector projector(1.0f, Conversions::RadToDeg(2.0f * halfFov), zNear, zFar);
	lightProj = projector.Compute3DProjection();
#endif

	CreateLightTransformation(lightView, lightProj);
}


void ShadowMap::CreatePerspectiveTransformation(const Vector3f& center, const Vector3f& lightDirection, float lightDistance, float worldRadius) {
	Matrix4f lightView, lightProj;

	if (lightDistance == 0.0f)
		lightDistance = 10.0f * worldRadius;
	m_lightPosition = center + lightDirection * lightDistance;
	lightView.LookAt(m_lightPosition, center, Vector3f(0.0f, 1.0f, 0.0f));
	float halfFov = std::atan(worldRadius / lightDistance);
	Projector projector(1.0f, Conversions::RadToDeg(2 * halfFov), lightDistance - worldRadius, lightDistance + worldRadius);
	lightProj = projector.Compute3DProjection();
	CreateLightTransformation(lightView, lightProj);
}


void ShadowMap::CreateOrthoTransformation(const Vector3f& center, const Vector3f& lightDirection, float lightOffset, const Vector3f& worldSize, const Vector3f& worldMin, const Vector3f& worldMax) {
	Matrix4f lightView, lightProj;

	if (lightOffset <= 0.0f) // fall back to a sensible default if the caller didn't supply a distance
		lightOffset = worldSize.Length();
	m_lightPosition = center + lightDirection * lightOffset;
	lightView.LookAt(m_lightPosition, center, Vector3f(0.0f, 1.0f, 0.0f));

	// transform view frustum to light space
	Vector4f corners[8] = {
		{ worldMin.X(), worldMin.Y(), worldMin.Z(), 1.0f },
		{ worldMax.X(), worldMin.Y(), worldMin.Z(), 1.0f },
		{ worldMin.X(), worldMax.Y(), worldMin.Z(), 1.0f },
		{ worldMax.X(), worldMax.Y(), worldMin.Z(), 1.0f },
		{ worldMin.X(), worldMin.Y(), worldMax.Z(), 1.0f },
		{ worldMax.X(), worldMin.Y(), worldMax.Z(), 1.0f },
		{ worldMin.X(), worldMax.Y(), worldMax.Z(), 1.0f },
		{ worldMax.X(), worldMax.Y(), worldMax.Z(), 1.0f }
	};

	Vector4f vMin{ FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
	Vector4f vMax{ -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (int i = 0; i < 8; i++) {
		Vector4f v = lightView * corners[i];
		vMin.Minimize(v);
		vMax.Maximize(v);
	}
	Projector projector;
	lightProj = projector.ComputeOrthoProjection(vMin.x, vMax.x, vMin.y, vMax.y, -vMax.z, -vMin.z);

	CreateLightTransformation(lightView, lightProj);
}


bool ShadowMap::Update(Vector3f center, Vector3f lightDirection, float lightOffset, Vector3f worldMin, Vector3f worldMax) {
	if (m_status < 0)
		return false;
	Vector3f worldSize = Vector3f::Abs(worldMax - worldMin);
	[[maybe_unused]] float worldRadius = worldSize.Length() * 0.5f; // only the _DEBUG perspective path (CreatePerspectiveTransformation) still uses this
	if (not center.IsValid()) 
#if 0
	{
		Vector3f f = baseRenderer.Matrices(0)->ModelView().Inverse() * Vector3f(0.0f, 0.0f, -1.0f);
		center += f * worldRadius; // baseRenderer.Matrices(0)->ModelView().F()* worldRadius;
	}
	else
#endif
		center = (worldMin + worldMax) * 0.5f;
	if ((m_status == 0) and not CreateMap(Vector2f(worldSize.X(), worldSize.Z())))
		return false;
	// top-down uses an orthographic light frustum centred on the map (height-independent); the regular
	// (forward) view uses the viewer-aligned frustum around the player.
	Vector3f mapCenter = (worldMin + worldMax) * 0.5f;
#ifdef _DEBUG
	static int trafoType = 2;
	if (trafoType == 2) {
		if (baseRenderer.HasPerspective(BaseRenderer::rpForward))
			CreateViewerAlignedTransformation(center, lightDirection, lightOffset, worldMin, worldMax);
		else
			CreateOrthoTransformation(mapCenter, lightDirection, lightOffset, worldSize, worldMin, worldMax);
	}
	else if (trafoType == 1)
		CreatePerspectiveTransformation(center, lightDirection, lightOffset, worldRadius);
	else
		CreateOrthoTransformation(mapCenter, lightDirection, lightOffset, worldSize, worldMin, worldMax);
#else
	if (baseRenderer.HasPerspective(BaseRenderer::rpForward))
		CreateViewerAlignedTransformation(center, lightDirection, lightOffset, worldMin, worldMax);
	else
		CreateOrthoTransformation(mapCenter, lightDirection, lightOffset, worldSize, worldMin, worldMax);
#endif
	return true;
}

// =================================================================================================
