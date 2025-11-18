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
	if (not m_map->Create(size, size, 1, { .name = "shadowmap", .colorBufferCount = 0, .depthBufferCount = 1, .vertexBufferCount = 0, .hasMRTs = false }))
		return false;
	m_status = 1;
	return true;
}


bool ShadowMap::StartRender(void) noexcept {
	if (not IsAvailable())
		return false;
	EnableCamera();
	baseRenderer.StartShadowPass();
	m_map->Enable(0, FBO::dbDepth);
	return true;
}


bool ShadowMap::StopRender(void) noexcept {
	if (not IsAvailable())
		return false;
	DisableCamera();
	m_map->Disable();
	return true;
}


bool ShadowMap::Update(Vector3f lightDirection, float lightOffset, Vector3f worldMin, Vector3f worldMax) {
	if (m_status < 0)
		return false;
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
	// light view
	center = (worldMin + worldMax) * 0.5f;
	lightOffset = 10;
	m_matrices->ModelView().LookAt(center - lightDirection * lightOffset, center, Vector3f(0.0f, 1.0f, 0.0f));

	Vector3f vMin{ FLT_MAX, FLT_MAX, FLT_MAX };
	Vector3f vMax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (int i = 0; i < 8; i++) {
		Vector3f v = m_matrices->ModelView() * m_frustumCorners[i];
		vMin.Minimize(v);
		vMax.Maximize(v);
	}
	if ((m_status == 0) and not CreateMap(Vector2f(vMax.X() - vMin.X(), vMax.Y() - vMin.Y())))
		return false;
	// light projection
	m_matrices->GetProjection() = baseRenderer.Matrices()->GetProjector().ComputeOrthoProjection(-35.f, 35.f, -35.f, 35.f, 1.f, 200.f); // vMin.X(), vMax.X(), vMin.Y(), vMax.Y(), -vMax.Z(), -vMin.Z());
	// shadow transformation = light projection * light view * inverse(camera)
	m_shadowTransform = camera->ModelView().Inverse();
	m_shadowTransform *= m_matrices->ModelView();
	m_shadowTransform *= m_matrices->GetProjection();
	return true;
}

// =================================================================================================
