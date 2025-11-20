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
	if (not (m_map = new FBO()))
		return false;
	int size;
	for (size = openGLStates.MaxTextureSize(); size >= 1024; size /= 2) {
		if (m_map->Create(size, size, 1, { .name = "shadowmap", .colorBufferCount = 0, .depthBufferCount = 1, .vertexBufferCount = 0, .hasMRTs = false })) {
			m_status = 1;
			return true;
		}
		m_maxShadowRadius *= 0.9f;
	}
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
	openGLStates.CullFace(GL_FRONT);
	return true;
}


bool ShadowMap::StopRender(void) noexcept {
	if (not IsReady())
		return false;
	DisableCamera();
	m_map->Disable();
	openGLStates.CullFace(GL_BACK);
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


void ShadowMap::CreateViewerAlignedTransformation(const Vector3f& center, const Vector3f& lightDirection, float lightDistance, float worldRadius) {
	Matrix4f lightView, lightProj;

	worldRadius = std::min(worldRadius, m_maxShadowRadius);
	if (lightDistance == 0.0f)
		lightDistance = 10.0f * worldRadius;

	// Lichtposition wie gehabt
	m_lightPosition = center + lightDirection * lightDistance;

	// Forward-Achse der Lichtkamera (vom Licht zum Center)
	Vector3f f = -lightDirection; // beide sind schon normalisiert

	// Wunsch-"Rechts"-Achse: viewDir in Ebene orthogonal zu f projizieren
	Vector3f viewDir = baseRenderer.Matrices(0)->ModelView().Inverse() * Vector3f(0.0f, 0.0f, -1.0f);
	viewDir.Normalize();

	float dotFV = f.Dot(viewDir);
	Vector3f s = viewDir - f * dotFV;      // liegt jetzt senkrecht zu f
	s.Normalize();                         // gewünschte X-Achse im Licht-Raum

	// Up-Vektor so wählen, dass lookAt(s) wirklich als "right" bekommt
	Vector3f upParam = s.Cross(f);
	upParam.Normalize();

	// View-Matrix mit ausgerichtetem Frustum
	lightView.LookAt(m_lightPosition, center, upParam);

	float halfFov = std::atan(worldRadius / lightDistance);
	lightProj = baseRenderer.Matrices()->GetProjector().Create(1.0f, Conversions::RadToDeg(2.0f * halfFov), lightDistance - worldRadius, lightDistance + worldRadius);

	CreateLightTransformation(lightView, lightProj);
}


void ShadowMap::CreatePerspectiveTransformation(const Vector3f& center, const Vector3f& lightDirection, float lightDistance, float worldRadius) {
	Matrix4f lightView, lightProj;

	if (lightDistance == 0.0f)
		lightDistance = 10.0f * worldRadius;
	m_lightPosition = center + lightDirection * lightDistance;
	lightView.LookAt(m_lightPosition, center, Vector3f(0.0f, 1.0f, 0.0f));
	float halfFov = std::atan(worldRadius / lightDistance);
	lightProj = baseRenderer.Matrices()->GetProjector().Create(1.0f, Conversions::RadToDeg(2 * halfFov), lightDistance - worldRadius, lightDistance + worldRadius);
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
	lightProj = baseRenderer.Matrices()->GetProjector().ComputeOrthoProjection(vMin.x, vMax.x, vMin.y, vMax.y, -vMax.z, -vMin.z);

	CreateLightTransformation(lightView, lightProj);
}


bool ShadowMap::Update(Vector3f center, Vector3f lightDirection, float lightOffset, Vector3f worldMin, Vector3f worldMax) {
	if (m_status < 0)
		return false;
	Vector3f worldSize = Vector3f::Abs(worldMax - worldMin);
	float worldRadius = worldSize.Length() * 0.5f;
	if (center.IsValid()) {
		Vector3f f = baseRenderer.Matrices(0)->ModelView().Inverse() * Vector3f(0.0f, 0.0f, -1.0f);
		center += f * worldRadius; // baseRenderer.Matrices(0)->ModelView().F()* worldRadius;
	}
	else
		center = (worldMin + worldMax) * 0.5f;
	if ((m_status == 0) and not CreateMap(Vector2f(worldSize.X(), worldSize.Z())))
		return false;
	static int trafoType = 2;
	if (trafoType == 2)
		CreateViewerAlignedTransformation(center, lightDirection, lightOffset, worldRadius);
	if (trafoType == 1)
		CreatePerspectiveTransformation(center, lightDirection, lightOffset, worldRadius);
	else
		CreateOrthoTransformation(center, lightDirection, worldSize, worldMin, worldMax);
	return true;
}


void ShadowMap::UpdateTransformation(void) { // needs to be called whenever mModelView for a shader using shadow mapping changes (e.g. for moving geometry)
	if (IsReady()) {
		m_modelViewTransform = m_lightTransform;
		m_modelViewTransform *= baseRenderer.Matrices(0)->ModelView().Inverse();
	}
}

// =================================================================================================
