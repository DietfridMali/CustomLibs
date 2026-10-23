#define NOMINMAX

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "conversions.hpp"
#include "xrview.h"
#include "xrhandler.h"
#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "base_displayhandler.h"
#include "rendertarget.h"
#include "projector.h"
#include "mesh.h"
#include "loghandler.h"

// =================================================================================================

static constexpr float	kFovRampTime = 0.2f;
static constexpr float	kFovHeadDeadzone = 7.5f;
static constexpr float	kBackgroundScale = 1.1f;
static constexpr float	kBackgroundMaxHeight = 160.0f;
static constexpr float	kBackgroundRadius = 10.0f;
static constexpr float	kBackgroundZNear = 0.1f;
static constexpr float	kBackgroundZFar = 100.0f;
static constexpr int	kBackgroundCols = 32;
static constexpr int	kBackgroundRows = 24;
static constexpr float	kMaskZNear = 0.1f;
static constexpr float	kMaskZFar = 100.0f;
static constexpr int	kPollInterval = 3000;
static constexpr int	kSwitchSlice = 33;

// =================================================================================================

bool XRView::Create(const char* appName, XRSwitchClient* switchClient, const GfxFeatureRequest& featureRequest)
{
	m_appName = String((appName and *appName) ? appName : "rendertools");
	m_switchClient = switchClient;
	m_featureRequest = featureRequest;

	bool isAvailable = xrHandler.Create(static_cast<const char*>(m_appName));

	xrHandler.EnableSession(true);
	return isAvailable;
}


bool XRView::Start(int timeout)
{
	if (xrHandler.HasSession() and not xrHandler.WaitUntilRunning(timeout)) {
		logHandler.Print("VR: the headset is not active, starting without VR\n");
		xrHandler.DestroySession();
		xrHandler.EnableSession(false);
		m_suspended = true;
	}
	baseRenderer.SetViewCount(xrHandler.HasSession() ? kMaxRenderViews : 1);
	m_nextPoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(kPollInterval);
	return xrHandler.HasSession();
}


void XRView::Destroy(void)
noexcept
{
	for (Mesh** mesh : { &m_visibilityMask, &m_background }) {
		if (*mesh) {
			(*mesh)->Destroy();
			delete *mesh;
			*mesh = nullptr;
		}
	}
	m_visibilityMaskVersion = 0;
	m_backgroundAspect = 0.0f;
	m_backgroundWidth = 0.0f;
}


bool XRView::IsActive(void) const
noexcept
{
	return xrHandler.HasSession() and (baseRenderer.ViewCount() > 1);
}

// =================================================================================================

void XRView::SetUnitsPerMeter(float unitsPerMeter)
noexcept
{
	m_unitsPerMeter = (unitsPerMeter > 0.0f) ? unitsPerMeter : 1.0f;
}


void XRView::SetFovScale(float fovScale)
noexcept
{
	m_fovScale = std::max(fovScale, cMinFov);
}


float XRView::FovScale(void) const
noexcept
{
	return m_applyFovScale ? 1.0f + (m_fovScale - 1.0f) * m_fovRamp : 1.0f;
}


void XRView::UpdateFovRamp(void)
noexcept
{
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	float step = m_hasRampTime ? std::chrono::duration<float>(now - m_rampTime).count() / kFovRampTime : 0.0f;
	Matrix4f head = xrHandler.HeadOrientation();
	bool isHeadForward = (head.m[2][2] > 0.0f) and (std::fabs(head.m[2][0]) <= std::sin(Conversions::DegToRad(kFovHeadDeadzone)));

	m_rampTime = now;
	m_hasRampTime = true;
	if (isHeadForward)
		m_fovRamp = std::min(m_fovRamp + step, 1.0f);
	else
		m_fovRamp = std::max(m_fovRamp - step, 0.0f);
}

// =================================================================================================

Matrix4f XRView::SetupEyeProjections(float zNear, float zFar)
noexcept
{
	Projector	projector{ 1.0f, 45.0f, zNear, zFar };
	Matrix4f	eyeProjections[kMaxRenderViews];
	float		fovScale = FovScale();
	float		tanX = 0.0f;
	float		tanY = 0.0f;

	for (int i = 0; i < kMaxRenderViews; ++i) {
		float l;
		float r;
		float u;
		float d;

		xrHandler.EyeTangents(i, l, r, u, d);
		l *= fovScale;
		r *= fovScale;
		u *= fovScale;
		d *= fovScale;
		m_eyeView[i] = xrHandler.EyeFromHead(i);
		m_eyeView[i].m[3] = glm::vec4(glm::vec3(m_eyeView[i].m[3]) * m_unitsPerMeter, 1.0f);
		m_eyeProjection[i] = projector.ComputeFrustum(l * zNear, r * zNear, d * zNear, u * zNear);
		eyeProjections[i] = m_eyeProjection[i] * m_eyeView[i];

		Matrix4f headFromEye = m_eyeView[i].Inverse();

		for (int c = 0; c < 8; ++c) {
			float		z = (c & 4) ? zFar : zNear;
			Vector4f	p = headFromEye * Vector4f{ ((c & 1) ? r : l) * z, ((c & 2) ? u : d) * z, -z, 1.0f };
			float		depth = -p.z;

			if (depth <= 0.0f)
				continue;
			tanX = std::max(tanX, std::fabs(p.x) / depth);
			tanY = std::max(tanY, std::fabs(p.y) / depth);
		}
	}

	Matrix4f projection = projector.ComputeFrustum(-tanX * zNear, tanX * zNear, -tanY * zNear, tanY * zNear);

	if (m_hasProjection and (std::memcmp(m_projection.AsArray(), projection.AsArray(), 16 * sizeof(float)) != 0))
		baseRenderer.RemoveViewProjections(m_projection);
	baseRenderer.SetViewProjections(projection, eyeProjections, m_eyeView);
	m_projection = projection;
	m_hasProjection = true;
	return projection;
}


Matrix4f XRView::EyeProjection(int view, const Matrix4f& projection) const
noexcept
{
	const BaseRenderer::ViewProjections* views = baseRenderer.FindViewProjections(projection);

	if (not views or (view < 0) or (view >= kMaxRenderViews))
		return projection;
	return views->eyeProjection[view];
}


Matrix4f XRView::EyeView(int view, const Matrix4f& projection) const
noexcept
{
	const BaseRenderer::ViewProjections* views = baseRenderer.FindViewProjections(projection);

	if (not views or (view < 0) or (view >= kMaxRenderViews))
		return Matrix4f::IDENTITY;
	return views->eyeView[view];
}


Vector3f XRView::EyeOffset(int view, const Matrix4f& projection) const
noexcept
{
	Matrix4f headFromEye = EyeView(view, projection).Inverse();

	return Vector3f{ headFromEye.m[3].x, headFromEye.m[3].y, headFromEye.m[3].z };
}


int XRView::EyeTransforms(const Matrix4f& projection, const Matrix4f& view, Matrix4f* eyeProjections, Matrix4f* eyeViews) const
noexcept
{
	int viewCount = baseRenderer.ViewCount();

	for (int i = 0; i < viewCount; ++i) {
		Matrix4f eyeFromHead = EyeView(i, projection);

		eyeViews[i] = eyeFromHead * view;
		eyeProjections[i] = EyeProjection(i, projection) * eyeFromHead.Inverse();
	}
	return viewCount;
}


int XRView::EyeOffsets(const Matrix4f& projection, const Matrix4f& view, Vector4f* offsets) const
noexcept
{
	int			viewCount = baseRenderer.ViewCount();
	Matrix4f	worldFromHead = view.Inverse();

	for (int i = 0; i < viewCount; ++i) {
		Vector3f eye = EyeOffset(i, projection);

		offsets[i] = worldFromHead * Vector4f{ eye.x, eye.y, eye.z, 0.0f };
	}
	return viewCount;
}


Matrix4f XRView::HeadView(void) const
noexcept
{
	if (not IsActive())
		return Matrix4f::IDENTITY;

	Matrix4f	view = xrHandler.HeadOrientation().Transpose();
	Vector3f	position = xrHandler.HeadPosition();

	view.Translate(-position.X() * m_unitsPerMeter, -position.Y() * m_unitsPerMeter, -position.Z() * m_unitsPerMeter);
	return view;
}


void XRView::FieldOfView(float& left, float& right, float& up, float& down) const
noexcept
{
	left = 0.0f;
	right = 0.0f;
	up = 0.0f;
	down = 0.0f;
	for (int i = 0; i < kMaxRenderViews; ++i) {
		float l;
		float r;
		float u;
		float d;

		xrHandler.EyeTangents(i, l, r, u, d);
		left = std::min(left, std::atan(l));
		right = std::max(right, std::atan(r));
		up = std::max(up, std::atan(u));
		down = std::min(down, std::atan(d));
	}
}

// =================================================================================================

bool XRView::BuildVisibilityMask(void)
{
	float	tangents[kMaxRenderViews][4];
	int		vertexCount = 0;
	int		indexCount = 0;

	for (int view = 0; view < kMaxRenderViews; ++view) {
		const AutoArray<Vector2f>*	vertices;
		const AutoArray<uint32_t>*	indices;

		xrHandler.EyeTangents(view, tangents[view][0], tangents[view][1], tangents[view][2], tangents[view][3]);
		if (xrHandler.VisibilityMask(view, vertices, indices)) {
			vertexCount += vertices->Length();
			indexCount += indices->Length();
		}
	}
	if (indexCount == 0)
		return false;
	if (m_visibilityMask and (xrHandler.VisibilityMaskVersion() == m_visibilityMaskVersion) and
		(std::memcmp(tangents, m_visibilityMaskTangents, sizeof(tangents)) == 0))
		return true;
	if (not m_visibilityMask) {
		if (not (m_visibilityMask = new (std::nothrow) Mesh()))
			return false;
		m_visibilityMask->Init(MeshTopology::Triangles, 1);
		m_visibilityMask->SetName(String("xr visibility mask"));
	}

	float*		vertexData = m_visibilityMask->Vertices().GfxData().Resize(vertexCount * 3);
	uint32_t*	indexData = m_visibilityMask->Indices().GfxData().Resize(indexCount);
	uint32_t	base = 0;

	if (not (vertexData and indexData))
		return false;
	for (int view = 0; view < kMaxRenderViews; ++view) {
		const AutoArray<Vector2f>*	vertices;
		const AutoArray<uint32_t>*	indices;

		if (not xrHandler.VisibilityMask(view, vertices, indices))
			continue;

		Matrix4f projection = xrHandler.EyeProjection(view, kMaskZNear, kMaskZFar);

		for (int i = 0; i < vertices->Length(); ++i) {
			const Vector2f&	v = (*vertices)[i];
			Vector4f		c = projection * Vector4f{ v.x, v.y, -1.0f, 1.0f };

			*vertexData++ = c.x / c.w;
			*vertexData++ = c.y / c.w;
			*vertexData++ = float(view);
		}
		for (int i = 0; i < indices->Length(); ++i)
			*indexData++ = base + (*indices)[i];
		base += uint32_t(vertices->Length());
	}
	m_visibilityMask->Vertices().SetDirty(true);
	m_visibilityMask->Indices().SetDirty(true);
	if (not m_visibilityMask->UpdateData())
		return false;
	m_visibilityMaskVersion = xrHandler.VisibilityMaskVersion();
	std::memcpy(m_visibilityMaskTangents, tangents, sizeof(tangents));
	return true;
}


void XRView::RenderVisibilityMask(void)
{
	if (not (IsActive() and xrHandler.HasVisibilityMask() and BuildVisibilityMask()))
		return;
	gfxStates.ColorMask(0, 0, 0, 0);
	gfxStates.SetDepthTest(1);
	gfxStates.SetDepthWrite(1);
	gfxStates.DepthFunc(GfxOperations::CompareFunc::Always);
	gfxStates.SetFaceCulling(0);
	gfxStates.SetBlending(0);

	Shader* shader = baseShaderHandler.SetupRenderShader("xrVisibilityMask");

	if (shader)
		m_visibilityMask->Render(static_cast<Texture*>(nullptr));
	gfxStates.ColorMask(1, 1, 1, 1);
	gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
}

// =================================================================================================

bool XRView::BuildBackground(float aspect)
{
	float right = 0.0f;

	for (int i = 0; i < kMaxRenderViews; ++i) {
		float l;
		float r;
		float u;
		float d;

		xrHandler.EyeTangents(i, l, r, u, d);
		right = std::max(right, std::max(std::atan(r), -std::atan(l)));
	}

	float width = kBackgroundScale * 2.0f * right;

	if (m_background and (m_backgroundAspect == aspect) and (m_backgroundWidth == width))
		return true;
	if (not m_background) {
		if (not (m_background = new (std::nothrow) Mesh()))
			return false;
		m_background->Init(MeshTopology::Triangles, 1);
		m_background->SetName(String("xr background"));
	}

	int			vertexCount = (kBackgroundCols + 1) * (kBackgroundRows + 1);
	int			indexCount = kBackgroundCols * kBackgroundRows * 6;
	float*		vertexData = m_background->Vertices().GfxData().Resize(vertexCount * 3);
	float*		texCoordData = m_background->TexCoords(0).GfxData().Resize(vertexCount * 2);
	uint32_t*	indexData = m_background->Indices().GfxData().Resize(indexCount);

	if (not (vertexData and texCoordData and indexData))
		return false;

	float height = std::min(width / aspect, Conversions::DegToRad(kBackgroundMaxHeight));

	for (int y = 0; y <= kBackgroundRows; ++y) {
		float v = float(y) / float(kBackgroundRows);
		float latitude = (0.5f - v) * height;

		for (int x = 0; x <= kBackgroundCols; ++x) {
			float u = float(x) / float(kBackgroundCols);
			float longitude = (u - 0.5f) * width;

			*vertexData++ = kBackgroundRadius * std::cos(latitude) * std::sin(longitude);
			*vertexData++ = kBackgroundRadius * std::sin(latitude);
			*vertexData++ = -kBackgroundRadius * std::cos(latitude) * std::cos(longitude);
			*texCoordData++ = u;
			*texCoordData++ = v;
		}
	}
	for (int y = 0; y < kBackgroundRows; ++y) {
		for (int x = 0; x < kBackgroundCols; ++x) {
			uint32_t i = uint32_t(y * (kBackgroundCols + 1) + x);

			*indexData++ = i;
			*indexData++ = i + 1;
			*indexData++ = i + kBackgroundCols + 1;
			*indexData++ = i + 1;
			*indexData++ = i + kBackgroundCols + 2;
			*indexData++ = i + kBackgroundCols + 1;
		}
	}
	m_background->Vertices().SetDirty(true);
	m_background->TexCoords(0).SetDirty(true);
	m_background->Indices().SetDirty(true);
	if (not m_background->UpdateData())
		return false;
	m_backgroundAspect = aspect;
	m_backgroundWidth = width;
	return true;
}


bool XRView::RenderBackground(Texture* texture, float aspect, const RGBAColor& tint, bool decodeSRGB, bool toEyes)
{
	if (not (IsActive() and texture and (aspect > 0.0f) and BuildBackground(aspect)))
		return false;

	RenderTarget* eyes = toEyes ? xrHandler.EyeTarget() : nullptr;

	if (toEyes) {
		if (not (eyes and eyes->Activate({ .clear = false })))
			return false;
		gfxStates.SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		gfxStates.ClearColorBuffers();
	}
	gfxStates.ColorMask(1, 1, 1, 1);
	gfxStates.SetDepthTest(0);
	gfxStates.SetDepthWrite(0);
	gfxStates.SetFaceCulling(0);
	gfxStates.SetBlending(0);

	Shader* shader = baseShaderHandler.SetupRenderShader("xrBackground");

	if (shader) {
		Matrix4f	viewProjections[kMaxRenderViews];
		Matrix4f	headFromApp = xrHandler.HeadOrientation().Transpose();

		for (int i = 0; i < kMaxRenderViews; ++i) {
			Matrix4f eyeFromHead = xrHandler.EyeFromHead(i);

			eyeFromHead.m[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			viewProjections[i] = xrHandler.EyeProjection(i, kBackgroundZNear, kBackgroundZFar) * eyeFromHead * headFromApp;
		}
		shader->SetMatrix4fArray("mViewProj", viewProjections[0].AsArray(), baseRenderer.ViewCount());
		shader->SetInt("bDecodeSRGB", decodeSRGB ? 1 : 0);
		shader->SetVector4f("tint", Vector4f{ tint.R() * tint.A(), tint.G() * tint.A(), tint.B() * tint.A(), 1.0f });
		m_background->Render(texture);
	}
	if (toEyes) {
		eyes->Deactivate();
		xrHandler.SetEyesRendered();
	}
	return shader != nullptr;
}


bool XRView::CoverEyes(const RGBAColor& color)
{
	RenderTarget* eyes = xrHandler.EyeTarget();

	if (not (xrHandler.EyesRendered() and eyes and (color.A() > 0.0f) and eyes->Activate({ .clear = false })))
		return false;
	baseRenderer.PushMatrix();
	baseRenderer.ResetTransformation();
	baseRenderer.Set2DRenderStates(1);
	gfxStates.BlendFunc(GfxOperations::BlendFactor::SrcAlpha, GfxOperations::BlendFactor::InvSrcAlpha);
	gfxStates.BlendEquation(GfxOperations::BlendOp::Add);
	baseRenderer.Fill(color);
	baseRenderer.PopMatrix();
	eyes->Deactivate();
	return true;
}

// =================================================================================================

void XRView::SetVSync(bool vSync)
noexcept
{
	m_vSync = vSync;
}


int XRView::PollHeadset(void)
{
	if (m_switching)
		return srNone;
	if (xrHandler.HasSession()) {
		if (not xrHandler.SessionStopped())
			return srNone;
		m_suspended = true;
		return srOff;
	}

	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (now < m_nextPoll)
		return srNone;
	m_nextPoll = now + std::chrono::milliseconds(kPollInterval);
	if (xrHandler.IsAvailable()) {
		if (xrHandler.InstanceLost())
			xrHandler.Destroy();
		else if (xrHandler.SystemAvailable()) {
			if (m_suspended and xrHandler.HeadsetReady())
				m_suspended = false;
		}
		else {
			m_suspended = false;
			return srNone;
		}
	}
	if (not (xrHandler.IsAvailable() or xrHandler.Create(static_cast<const char*>(m_appName)))) {
		m_suspended = false;
		return srNone;
	}
	return m_suspended ? srNone : srOn;
}


int XRView::BeginSwitch(bool on)
{
	if (m_switching or (on == xrHandler.HasSession()))
		return 0;
	m_switching = true;
	m_switchOn = on;
	m_switchStep = 0;
	m_switchFull = on and not xrHandler.DeviceCreated();
	m_switchSteps = m_switchFull ? 2 : baseShaderHandler.ShaderCount() + 2;
	logHandler.Print("VR: switching %s%s\n", on ? "on" : "off", m_switchFull ? " with a new graphics device" : "");
	return m_switchSteps;
}


bool XRView::StepSwitch(void)
{
	if (not m_switching)
		return false;

	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

	if (m_switchStep == 0) {
		if (m_switchFull)
			RestartGraphics(m_switchOn);
		else
			SwitchSession(m_switchOn);
		m_switchStep = 1;
		return true;
	}
	if (m_switchFull) {
		EndSwitch();
		return false;
	}

	int shaderCount = m_switchSteps - 2;

	if (m_switchStep > shaderCount) {
		EndSwitch();
		return false;
	}
	gfxStates.Finish();
	do {
		baseShaderHandler.RecreateShader(m_switchStep - 1);
		++m_switchStep;
	} while ((m_switchStep <= shaderCount) and
			 (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(kSwitchSlice)));
	return true;
}


float XRView::SwitchProgress(void) const
noexcept
{
	return (m_switchSteps > 0) ? std::min(float(m_switchStep) / float(m_switchSteps), 1.0f) : 1.0f;
}


bool XRView::SwitchSession(bool on)
{
	gfxStates.Finish();
	if (m_switchClient)
		m_switchClient->ReleaseViewBuffers();
	xrHandler.EnableSession(on);
	if (not on)
		xrHandler.DestroySession();
	else if (not xrHandler.CreateSession(xrHandler.LayerWidth(), xrHandler.LayerHeight())) {
		logHandler.Print("VR: the OpenXR session could not be created\n");
		xrHandler.EnableSession(false);
		m_suspended = true;
	}
	UpdateViewCount();
	if (m_switchClient)
		m_switchClient->CreateViewBuffers();
	return xrHandler.HasSession() == on;
}


bool XRView::RestartGraphics(bool on)
{
	gfxStates.Finish();
	if (m_switchClient) {
		m_switchClient->ReleaseViewBuffers();
		m_switchClient->ReleaseDeviceData();
	}
	if (on and not xrHandler.IsAvailable())
		xrHandler.Create(static_cast<const char*>(m_appName));
	xrHandler.EnableSession(on and xrHandler.IsAvailable());
	if (not baseRenderer.RestartGraphics(m_featureRequest)) {
		logHandler.Print("VR: the graphics device could not be restarted\n");
		return false;
	}
	m_graphicsRestarted = true;
	UpdateViewCount();
	if (m_switchClient)
		m_switchClient->CreateViewBuffers();
	return xrHandler.HasSession() == on;
}


void XRView::UpdateViewCount(void)
{
	bool hasSession = xrHandler.HasSession();

	baseRenderer.SetViewCount(hasSession ? kMaxRenderViews : 1);
	if (baseRenderer.ScreenBuffer())
		baseRenderer.ScreenBuffer()->SetCoverageAlpha(hasSession);
	if (m_hasProjection) {
		baseRenderer.RemoveViewProjections(m_projection);
		m_hasProjection = false;
	}
}


void XRView::EndSwitch(void)
{
	baseDisplayHandler.SetVSync(m_vSync and not xrHandler.HasSession());
	if (xrHandler.HasSession())
		xrHandler.Recenter();
	m_switching = false;
	if (m_switchClient)
		m_switchClient->FinishSwitch();
	logHandler.Print("VR: %s\n", xrHandler.HasSession() ? "on" : "off");
}


bool XRView::GraphicsRestarted(void)
noexcept
{
	bool graphicsRestarted = m_graphicsRestarted;

	m_graphicsRestarted = false;
	return graphicsRestarted;
}

// =================================================================================================
