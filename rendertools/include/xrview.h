#pragma once

#include <cstdint>
#include <chrono>

#include "basesingleton.hpp"
#include "array.hpp"
#include "string.hpp"
#include "vector.hpp"
#include "matrix.hpp"
#include "colordata.h"
#include "rendertypes.h"

class Mesh;
class Shader;
class Texture;

// =================================================================================================

class XRSwitchClient {
public:
	virtual ~XRSwitchClient() = default;

	virtual void ReleaseViewBuffers(void) = 0;

	virtual void CreateViewBuffers(void) = 0;

	virtual void ReleaseDeviceData(void) = 0;

	virtual void FinishSwitch(void) = 0;
};

// =================================================================================================

class XRView : public BaseSingleton<XRView> {
public:
	enum eSwitchRequest {
		srNone = -1,
		srOff = 0,
		srOn = 1
	};

	static constexpr float	cMinFov = 1.0f;
	static constexpr float	cMaxFov = 1.5f;

private:
	float			m_unitsPerMeter{ 1.0f };
	float			m_viewPitch{ 0.0f };
	float			m_fovScale{ cMaxFov };
	float			m_fovRamp{ 0.0f };
	bool			m_applyFovScale{ false };
	std::chrono::steady_clock::time_point	m_rampTime{};
	bool			m_hasRampTime{ false };

	Matrix4f		m_projection{};
	Matrix4f		m_eyeProjection[kMaxRenderViews]{};
	Matrix4f		m_eyeView[kMaxRenderViews]{};
	bool			m_hasProjection{ false };

	Mesh*			m_visibilityMask{ nullptr };
	uint32_t		m_visibilityMaskVersion{ 0 };
	float			m_visibilityMaskTangents[kMaxRenderViews][4]{};

	Mesh*			m_background{ nullptr };
	float			m_backgroundAspect{ 0.0f };
	float			m_backgroundWidth{ 0.0f };

	String			m_appName;
	XRSwitchClient*	m_switchClient{ nullptr };
	GfxFeatureRequest	m_featureRequest{};
	bool			m_vSync{ false };
	bool			m_suspended{ false };
	std::chrono::steady_clock::time_point	m_nextPoll{};
	bool			m_decodeSRGB{ false };
	bool			m_graphicsRestarted{ false };
	bool			m_switching{ false };
	bool			m_switchOn{ false };
	bool			m_switchFull{ false };
	int				m_switchStep{ 0 };
	int				m_switchSteps{ 0 };

public:
	bool Create(const char* appName, XRSwitchClient* switchClient, const GfxFeatureRequest& featureRequest = {});

	bool Start(int timeout);

	void Destroy(void)
	noexcept;

	bool IsActive(void) const
	noexcept;

	void SetUnitsPerMeter(float unitsPerMeter)
	noexcept;

	inline float UnitsPerMeter(void) const
	noexcept
	{
		return m_unitsPerMeter;
	}

	void SetViewPitch(float degrees)
	noexcept;

	void SetFovScale(float fovScale)
	noexcept;

	inline float ConfiguredFovScale(void) const
	noexcept
	{
		return m_fovScale;
	}

	inline void ApplyFovScale(bool apply)
	noexcept
	{
		m_applyFovScale = apply;
	}

	float FovScale(void) const
	noexcept;

	void UpdateFovRamp(void)
	noexcept;

	Matrix4f SetupEyeProjections(float zNear, float zFar)
	noexcept;

	Matrix4f EyeProjection(int view, const Matrix4f& projection) const
	noexcept;

	Matrix4f EyeView(int view, const Matrix4f& projection) const
	noexcept;

	Vector3f EyeOffset(int view, const Matrix4f& projection) const
	noexcept;

	int EyeTransforms(const Matrix4f& projection, const Matrix4f& view, Matrix4f* eyeProjections, Matrix4f* eyeViews) const
	noexcept;

	int EyeOffsets(const Matrix4f& projection, const Matrix4f& view, Vector4f* offsets) const
	noexcept;

	Matrix4f HeadView(void) const
	noexcept;

	void FieldOfView(float& left, float& right, float& up, float& down) const
	noexcept;

	void RenderVisibilityMask(void);

	bool RenderBackground(Texture* texture, float aspect, const RGBAColor& tint, bool decodeSRGB, bool toEyes);

	bool CoverEyes(const RGBAColor& color);

	inline void SetDecodeSRGB(bool decodeSRGB)
	noexcept
	{
		m_decodeSRGB = decodeSRGB;
	}

	inline bool DecodeSRGB(void) const
	noexcept
	{
		return m_decodeSRGB;
	}

	void SetVSync(bool vSync)
	noexcept;

	int PollHeadset(void);

	int BeginSwitch(bool on);

	bool StepSwitch(void);

	float SwitchProgress(void) const
	noexcept;

	bool GraphicsRestarted(void)
	noexcept;

	inline bool IsSwitching(void) const
	noexcept
	{
		return m_switching;
	}

private:
	Matrix4f ViewPitchShear(void) const
	noexcept;

	bool BuildVisibilityMask(void);

	bool BuildBackground(float aspect);

	bool SwitchSession(bool on);

	bool RestartGraphics(bool on);

	void UpdateViewCount(void);

	void EndSwitch(void);
};

#define xrView XRView::Instance()

// =================================================================================================
