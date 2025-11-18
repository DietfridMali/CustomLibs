#include "shadowmap.h"

// =================================================================================================

void ShadowMap::Setup(void) {
	m_matrixIndex = baseRenderer.AddMatrices();
	m_matrices = baseRenderer.Matrices(m_matrixIndex);
}


bool ShadowMap::CreateMap(Vector2f frustumSize) {
	m_status = -1;
	if (not (m_map = new FBO()))
		return false;
	int resolution = int(round(std::max(frustumSize.x / 1.f, frustumSize.y / 1.f)));
	int size;
	for (size = 1; size < resolution; size <<= 1)
		;
	size = 2048;
	if (not m_map->Create(size, size, 1, { .name = "shadowmap", .colorBufferCount = 0, .depthBufferCount = 1, .vertexBufferCount = 0, .hasMRTs = false }))
		return false;
	m_status = 1;
	return true;
}


void ShadowMap::Destroy(void) noexcept {
	if (m_map) {
		delete m_map;
		m_map = nullptr;
		m_status = 0;
	}
}


bool ShadowMap::StartRender(void) noexcept {
	if (not IsAvailable())
		return false;
	baseRenderer.StartShadowPass();
	m_map->Enable(0, FBO::dbDepth);
	glClear(GL_DEPTH_BUFFER_BIT);
	EnableCamera();
	return true;
}


bool ShadowMap::StopRender(void) noexcept {
	if (not IsAvailable())
		return false;
	DisableCamera();
	m_map->Disable();
	return true;
}


void ShadowMap::Stabilize(float shadowMapSize)
{
	// Ursprungs-Punkt (0,0,0) in Shadow-Space
	Vector4f shadowOrigin = m_shadowTransform * Vector4f(0.0f, 0.0f, 0.0f, 1.0f);

	// in Texelraum skalieren
	shadowOrigin *= shadowMapSize * 0.5f;

	Vector2f roundedOrigin = Vector2f::Round(Vector2f(shadowOrigin.x, shadowOrigin.y));
	Vector2f offset = (roundedOrigin - Vector2f(shadowOrigin.x, shadowOrigin.y)) * (2.0f / shadowMapSize);

	// Translation der Shadow-Matrix korrigieren
	m_shadowTransform.T().x += offset.x;
	m_shadowTransform.T().y += offset.y;
}



bool ShadowMap::Update(Vector3f center, Vector3f lightDirection, float lightOffset, Vector3f worldMin, Vector3f worldMax) {
	if (m_status < 0)
		return false;
#if 0
	MatrixStack* camera = baseRenderer.Matrices(0);
	Matrix4f m = (camera->GetProjection() * camera->ModelView()).Inverse();
	Vector3f center = Vector3f::ZERO;
	for (int i = 0; i < 8; i++) {
		Vector4f p = m * m_ndcCorners[i];
		m_frustumCorners[i] = Vector3f(p) / p.w;
		m_frustumCorners[i].Maximize(worldMin);
		m_frustumCorners[i].Minimize(worldMax);
		center += m_frustumCorners[i];
	}
	center /= 8.0f;
#endif

	// light view
	//if (not center.IsValid())
		center = (worldMin + worldMax) * 0.5f;
	Vector3f worldSize = worldMax - worldMin;
	lightOffset = sqrt(worldSize.Length());
	lightOffset = worldSize.Length();
	m_matrices->ModelView().LookAt(center + lightDirection * lightOffset, center, Vector3f(0.0f, 1.0f, 0.0f));

#if 0
	Vector3f corners[8] = {
		{ worldMin.X(), worldMin.Y(), worldMin.Z() },
		{ worldMax.X(), worldMin.Y(), worldMin.Z() },
		{ worldMin.X(), worldMax.Y(), worldMin.Z() },
		{ worldMax.X(), worldMax.Y(), worldMin.Z() },
		{ worldMin.X(), worldMin.Y(), worldMax.Z() },
		{ worldMax.X(), worldMin.Y(), worldMax.Z() },
		{ worldMin.X(), worldMax.Y(), worldMax.Z() },
		{ worldMax.X(), worldMax.Y(), worldMax.Z() }
	};

	Vector3f vMin{ FLT_MAX, FLT_MAX, FLT_MAX };
	Vector3f vMax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (int i = 0; i < 8; i++) {
		Vector3f v = m_matrices->ModelView() * corners[i];
		vMin.Minimize(v);
		vMax.Maximize(v);
	}
	if ((m_status == 0) and not CreateMap(Vector2f(vMax.X() - vMin.X(), vMax.Z() - vMin.Z())))
		return false;
#if 0
	// light projection
	float s = std::max(vMax.x - vMin.x, vMax.y - vMin.y);
	s *= sqrtf(2.0f);
	m_matrices->GetProjection() = baseRenderer.Matrices()->GetProjector().ComputeOrthoProjection(-s, s, -s, s, 1.0f, 200.0f);
#	else
	m_matrices->GetProjection() = baseRenderer.Matrices()->GetProjector().ComputeOrthoProjection(vMin.x, vMax.x, vMin.y, vMax.y, vMin.z, vMax.z);
#	endif
#else
	if ((m_status == 0) and not CreateMap(Vector2f(worldSize.X(), worldSize.Y())))
		return false;
	// light projection
#	if 0
	m_matrices->GetProjection() = baseRenderer.Matrices()->GetProjector().Create(1.0f, 45.0f, 1.0f, 200.0f);
#	else
	float s = std::max(worldSize.X(), worldSize.Z());
	//s *= 0.5f; // sqrtf(2.0f) * 0.5f;
	m_matrices->GetProjection() = baseRenderer.Matrices()->GetProjector().ComputeOrthoProjection(-s, s, -s, s, 0.1f, 200.0f);
#	endif
#endif
	// shadow transformation = light projection * light view * inverse(camera)
	m_shadowTransform = m_matrices->GetProjection();
	m_shadowTransform *= m_matrices->ModelView();
#if 0 // not needed, working with world coordinates in the shaders that get directly transformed into shadow space for the shadow map lookup
	m_shadowTransform *= camera->ModelView().Inverse();
#endif
	Stabilize(float (m_map->GetWidth(true)));
	return true;
}

// =================================================================================================
