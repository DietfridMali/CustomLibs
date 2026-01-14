#include "shadowmap.h"

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
	if (not (m_map = new FBO()))
		return false;
	int size;
	for (size = openGLStates.MaxTextureSize(); size >= 1024; size /= 2) {
		if (m_map->Create(size, size, 1, { .name = "shadowmap", .colorBufferCount = 0, .depthBufferCount = 1, .vertexBufferCount = 0, .hasMRTs = false })) {
			m_status = 1;
			return true;
		}
		m_maxLightRadius *= 0.9f;
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
	m_map->Enable(0, FBO::dbDepth);
	glClear(GL_DEPTH_BUFFER_BIT);
	EnableCamera();
	openGLStates.SetDepthTest(1);
	openGLStates.SetDepthWrite(1);
#if 1
	openGLStates.CullFace(GL_FRONT);
#else
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(2.0f, 4.0f);
#endif
	return true;
}


bool ShadowMap::StopRender(void) noexcept {
	if (not IsReady())
		return false;
	DisableCamera();
	m_map->Disable();
#if 1
	openGLStates.CullFace(GL_BACK);
#else
	glDisable(GL_POLYGON_OFFSET_FILL);
#endif
	return true;
}


void ShadowMap::Stabilize(float shadowMapSize)
{
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


void ShadowMap::CreateViewerAlignedTransformation(Vector3f center, const Vector3f& lightDirection, float lightDistance, float worldRadius) {
	Matrix4f lightView, lightProj;

	worldRadius = std::min(worldRadius, m_maxLightRadius);
	if (lightDistance <= 0.0f)
		lightDistance = 10.0f * worldRadius;

	// Viewer-Blickrichtung in Weltkoordinaten
	Vector3f viewDir = baseRenderer.Matrices(0)->ModelView().Inverse() * Vector3f(0.0f, 0.0f, -1.0f);
	viewDir.Normalize();

	// Frustumzentrum vor den Viewer schieben (Viewer knapp am hinteren Rand)
	center += viewDir * worldRadius * 0.8f;

	Vector3f f = -lightDirection; // angenommen normalisiert
	float dotFV = f.Dot(viewDir);

	// X-Achse im Lichtraum: Projektionsanteil der Viewrichtung senkrecht zu f
	Vector3f s = viewDir - f * dotFV;      // liegt senkrecht zu f
	s.Normalize();

	// Up-Vektor so wählen, dass LookAt(s) als Right bekommt
	Vector3f upParam = s.Cross(f);
	upParam.Normalize();

	m_lightPosition = center + lightDirection * lightDistance;

	lightView.LookAt(m_lightPosition, center, upParam);

	float halfFov = std::atan(worldRadius / lightDistance);
	float zNear = std::max(0.01f, lightDistance - worldRadius);
	float zFar = lightDistance + worldRadius;

	Projector projector(1.0f, Conversions::RadToDeg(2.0f * halfFov), zNear, zFar);
	lightProj = projector.Compute3DProjection();

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


void ShadowMap::CreateOrthoTransformation(const Vector3f& center, const Vector3f& lightDirection, const Vector3f& worldSize, const Vector3f& worldMin, const Vector3f& worldMax) {
	Matrix4f lightView, lightProj;

	float lightOffset = worldSize.Length();
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
	float worldRadius = worldSize.Length() * 0.5f;
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
#ifdef _DEBUG
	static int trafoType = 2;
	if (trafoType == 2)
		CreateViewerAlignedTransformation(center, lightDirection, lightOffset, worldRadius);
	else if (trafoType == 1)
		CreatePerspectiveTransformation(center, lightDirection, lightOffset, worldRadius);
	else
		CreateOrthoTransformation(center, lightDirection, worldSize, worldMin, worldMax);
#else
	CreateViewerAlignedTransformation(center, lightDirection, lightOffset, worldRadius);
#endif
	return true;
}

// =================================================================================================
