#include "shadowmap.h"
#include "mapsegment.h"
#include "maphandler.h"

// =================================================================================================

void ShadowMap::Setup(void) {
	m_matrixIndex = renderer.AddMatrices();
	m_matrices = renderer.Matrices(m_matrixIndex);
}


bool ShadowMap::CreateMap(Vector2f frustumSize) {
	m_status = -1;
	if (not (m_map = new FBO()))
		return false;
	int resolution = int(round(std::max(frustumSize.x / 0.1f, frustumSize.y / 0.1f)));
	int size;
	for (size = 1; size < resolution; size <<= 1)
		;
	if (not m_map->Create(size, size, 1, { .name = "shadowmap", .colorBufferCount = 0, .depthBufferCount = 1, .vertexBufferCount = 0, .hasMRTs = false }))
		return false;
	m_status = 1;
	return true;
}


bool ShadowMap::Update(Vector3f lightDirection, float lightOffset) {
	if (m_status < 0)
		return false;
	renderer.EnableCamera();
	Matrix4f m = (renderer.Projection() * renderer.ModelView()).Inverse();
	renderer.DisableCamera();
	Vector3f center = Vector3f::ZERO;
	for (int i = 0; i < 8; i++) {
		Vector4f p = m * m_ndcCorners[i];
		m_frustumCorners[i] = Vector3f(p) / p.w;
		center += m_frustumCorners[i];
	}
	center /= 8.0f;
	// light view
	m_matrices->ModelView().LookAt(center + lightDirection * lightOffset, center, Vector3f(0.0f, 1.0f, 0.0f));

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
	m_matrices->GetProjection() = renderer.Matrices()->GetProjector().ComputeOrthoProjection(vMin.X(), vMax.X(), vMin.Y(), vMax.Y(), -vMax.Z(), -vMin.Z());
	// shadow transformation = light projection * light view * inverse(camera)
	m_shadowTransform = renderer.ModelView().Inverse();
	m_shadowTransform *= m_matrices->ModelView();
	m_shadowTransform *= m_matrices->GetProjection();
	return true;
}

using RenderTypes = MapSegment::RenderTypes;
using enum RenderTypes;


void ShadowMap::Render(Vector3f lightDirection, float lightOffset) {
	if (Update(lightDirection, lightOffset)) {
		renderer.SelectMatrixStack(m_matrixIndex);
		renderer.StartDepthPass();
		m_map->Enable();
		mapHandler.RenderPass(RenderPassType::rpDepth);
		m_map->Disable();
		renderer.PopMatrix(RenderMatrices::mtProjection);
		renderer.PopMatrix();
		renderer.SelectMatrixStack(0);
	}
}

// =================================================================================================
