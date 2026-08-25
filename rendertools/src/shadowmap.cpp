#include "shadowmap.h"

#define APPLY_POLYGON_OFFSET 1

// LiSPSM (Light Space Perspective Shadow Maps, Wimmer/Scherzer/Purgathofer 2004).
// 1 = warped light frustum: same focused region as before, but texel density is redistributed
//     towards the near field (~1/z) instead of being spread uniformly -> kills the near-field
//     stair-stepping that soft shadows only mask.
// 0 = the previous viewer-aligned perspective frustum (uniform density).
// Both live in CreateViewerAlignedTransformation(); only that function branches on this.
//
// SET TO 0 on 2026-08-25 while a shadow offset on the smileys is being tracked down. Reasoning: the
// warped path is the ONLY change to the shadow chain since the shadows last looked right (added
// 2026-08-03, never verified in-game), and it ships with a bias that was admittedly never retuned
// for it -- see the BIAS NOTE in CreateViewerAlignedTransformation(). The stencil work of
// 2026-08-24 is ruled out: no render target in the game requests a stencil plane (CreateMap below
// asks for depthBufferCount = 1 and nothing else), so every format there is bit-identical to before.
// Flip this back to 1 to retest once the bias behaves.
#define LiSPSM 0

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


// Forward view. Three variants, all focusing the same region in front of the viewer (centre shifted
// 0.8*radius forward, so the viewer sits near the rear edge):
//   #if LiSPSM   warped light frustum, near-field texel density ~1/z  -- currently OFF (see #define LiSPSM)
//   #elif 0      viewer-focused ORTHO, kept for reference: no perceptible benefit over the perspective
//                variant, only crawling stepped edges (even at 4K). Its texel-snap (Stabilize) damps
//                translation but not the sun's slow rotation, and ortho spreads texels uniformly, so
//                the near field stays coarse. Re-enabling also needs the m_lightTransform texel-snap.
//   #else        viewer-aligned PERSPECTIVE frustum -- the previous default. At lightDistance 1000 and
//                radius <= 15 its halfFov is ~0.86 deg, i.e. numerically indistinguishable from ortho;
//                the density is uniform, which is what leaves the near-field stair-stepping in place.
void ShadowMap::CreateViewerAlignedTransformation(Vector3f center, const Vector3f& lightDirection, float lightDistance, const Vector3f& worldMin, const Vector3f& worldMax) {
	Matrix4f lightView, lightProj;

#if LiSPSM

	// LiSPSM: the focused region is the same as in the #else path (worldRadius around a centre shifted
	// 0.8*radius forward), but the light frustum is warped along the viewer's forward axis so near-field
	// texels get denser. The warp axis is the view direction made perpendicular to the light, and it is
	// handed to LookAt as the UP vector -- that is the whole trick: afterwards the forward axis lies on
	// light-space +y, and the perspective warp below runs along y ONLY. x (perpendicular to both view and
	// light) stays unwarped, which is what distinguishes LiSPSM from plain PSM.
	//
	// BIAS NOTE: the warp makes NDC z non-linear over the region. The constant ShadowBias() offset in the
	// shaders and SetPolygonOffset() in StartRender() were tuned against the #else path, which is
	// effectively linear (halfFov ~0.86 deg at lightDistance 1000). Expect to retune them: the same NDC
	// offset now corresponds to a smaller world distance near the viewer and a larger one far away.

	float worldRadius = std::min(Vector3f::Abs(worldMax - worldMin).Length() * 0.5f, m_maxLightRadius);
	if (lightDistance <= 0.0f)
		lightDistance = 100.0f * worldRadius;
	Vector3f viewDir = baseRenderer.Matrices(0)->ModelView().Inverse() * Vector3f(0.0f, 0.0f, -1.0f);
	viewDir.Normalize();
	center += viewDir * worldRadius * 0.8f;   // viewer near rear edge

	Vector3f lightVec = -lightDirection;                      // direction the light travels
	Vector3f warpAxis = viewDir - lightVec * lightVec.Dot(viewDir);   // view direction perpendicular to the light
	float sinGamma = warpAxis.Length();                       // both operands are unit -> this IS sin(gamma)

	// Looking (anti)parallel to the light degenerates the warp axis. Rebuild it from any perpendicular
	// axis so LookAt stays well-defined; the warp itself is then disabled via the sinGamma clamp below.
	static constexpr float minSinGamma = 0.15f;
	if (sinGamma < 1.0e-4f) {
		Vector3f fallback = (std::fabs(lightVec.Y()) < 0.9f) ? Vector3f(0.0f, 1.0f, 0.0f) : Vector3f(1.0f, 0.0f, 0.0f);
		warpAxis = fallback - lightVec * lightVec.Dot(fallback);
	}
	warpAxis.Normalize();
	// n_opt grows as 1/sin(gamma); clamping sinGamma from below makes the warp fade smoothly towards
	// ortho near the degenerate case instead of popping.
	float sinGammaClamped = std::max(sinGamma, minSinGamma);

	m_lightPosition = center + lightDirection * lightDistance;
	lightView.LookAt(m_lightPosition, center, warpAxis);

	// The shadowed body: the focused region in x/z, the full world height in y (same box the disabled
	// ortho variant below uses).
	Vector4f corners[8] = {
		{ center.X() - worldRadius, worldMin.Y(), center.Z() - worldRadius, 1.0f },
		{ center.X() + worldRadius, worldMin.Y(), center.Z() - worldRadius, 1.0f },
		{ center.X() - worldRadius, worldMax.Y(), center.Z() - worldRadius, 1.0f },
		{ center.X() + worldRadius, worldMax.Y(), center.Z() - worldRadius, 1.0f },
		{ center.X() - worldRadius, worldMin.Y(), center.Z() + worldRadius, 1.0f },
		{ center.X() + worldRadius, worldMin.Y(), center.Z() + worldRadius, 1.0f },
		{ center.X() - worldRadius, worldMax.Y(), center.Z() + worldRadius, 1.0f },
		{ center.X() + worldRadius, worldMax.Y(), center.Z() + worldRadius, 1.0f }
	};

	Vector4f vMin{ FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
	Vector4f vMax{ -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for (int i = 0; i < 8; i++) {
		Vector4f v = lightView * corners[i];
		vMin.Minimize(v);
		vMax.Maximize(v);
	}

	// n_opt = (zn + sqrt(zn*zf)) / sin(gamma). zn/zf are CAMERA distances along the view direction; the
	// sinGamma division converts them onto the warp axis. zn is deliberately clamped far above the camera
	// near plane -- a tiny zn drags n_opt down, which over-warps the near field and starves the far field.
	static constexpr float lispsmNearClamp = 0.5f;
	float zn = lispsmNearClamp;
	float zf = std::max(zn + 0.01f, 1.8f * worldRadius);   // body reaches 0.8*R + R ahead of the viewer
	float n = (zn + std::sqrt(zn * zf)) / sinGammaClamped;
	float d = std::max(vMax.y - vMin.y, 0.01f);            // body extent along the warp axis

	// Shift the body so its near edge sits at y = n. Every y is then >= n > 0, so the divide by w = y
	// below can never blow up or flip sign.
	Matrix4f warpShift = Matrix4f::Translation(0.0f, n - vMin.y, 0.0f);

	// Perspective warp along y: (x, y, z, 1) -> (x, (a*y + b)/n, z, y/n). After the divide by w this
	// maps y = n to -1 and y = n+d to +1, while x and z get squeezed by n/y -- near field keeps its
	// scale, far field shrinks, which is exactly the texel redistribution we are after.
	//
	// The whole matrix is divided by n (homogeneous matrices are only defined up to a scalar, so NDC is
	// bit-identical either way). That makes w = y/n, i.e. w == 1 at the near edge and w == 1 + d/n at the
	// far edge -- exactly the factor by which the shadow bias has to grow along the warp axis. The shaders
	// therefore just do "bias *= shadowCoord.w" and need no n_opt uniform. See the bias note above.
	float a = (2.0f * n + d) / d;
	float b = -2.0f * n * (n + d) / d;
	float rn = 1.0f / n;
	Matrix4f warp({   // Matrix4f(initializer_list) feeds glm::make_mat4 -> COLUMN major: each row here is one column
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, a * rn, 0.0f, rn,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, b * rn, 0.0f, 0.0f
		});
	Matrix4f warpTransform = warp * warpShift;

	// Normalize the warped body onto the unit cube. Taking the bounds AFTER the perspective divide is
	// legitimate because the ortho matrix is affine and leaves w untouched, so ortho*(warp*p) divided by w
	// equals ortho applied to the already divided point. Going through ComputeOrthoProjection also inherits
	// whatever depth convention the backend's glm build uses, exactly like CreateOrthoTransformation.
	Matrix4f warpedView = warpTransform * lightView;
	float xMin = FLT_MAX, xMax = -FLT_MAX, yMin = FLT_MAX, yMax = -FLT_MAX, zMin = FLT_MAX, zMax = -FLT_MAX;
	for (int i = 0; i < 8; i++) {
		Vector4f v = warpedView * corners[i];
		float rw = 1.0f / v.w;
		float x = v.x * rw, y = v.y * rw, z = v.z * rw;
		xMin = std::min(xMin, x); xMax = std::max(xMax, x);
		yMin = std::min(yMin, y); yMax = std::max(yMax, y);
		zMin = std::min(zMin, z); zMax = std::max(zMax, z);
	}
	Projector projector;
	// z is negative in front of the light camera (LookAt looks down -z), same negation as the ortho path.
	lightProj = projector.ComputeOrthoProjection(xMin, xMax, yMin, yMax, -zMax, -zMin);
	lightProj *= warpTransform;

#elif 0 // viewer-focused ORTHO frustum (disabled). Re-enabling also needs the m_lightTransform texel-snap
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
	// Normalize w the same way the LiSPSM branch does, so that "bias *= shadowCoord.w" in the shaders is
	// path-agnostic: here w is the light-space depth (~lightDistance), so dividing the whole matrix by it
	// yields w ~= 1 over the whole region (it varies by only +-worldRadius/lightDistance, ~1.5%). Scaling a
	// homogeneous matrix changes nothing about the resulting NDC. The ortho paths already have w == 1.
	{
		float rd = 1.0f / lightDistance;
		lightProj = Matrix4f({ rd, 0.0f, 0.0f, 0.0f,
							 0.0f,   rd, 0.0f, 0.0f,
							 0.0f, 0.0f,   rd, 0.0f,
							 0.0f, 0.0f, 0.0f,   rd }) * lightProj;
	}
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
