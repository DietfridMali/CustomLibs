#pragma once

#include <cstdint>

#include "array.hpp"
#include "string.hpp"
#include "vector.hpp"
#include "matrix.hpp"
#include "rendertypes.h"
#include "log_frame_metrics.h"

class RenderTarget;
class Texture;
struct XRState;
struct XRSwapchain;

// =================================================================================================

class BaseXRHandler {
public:
	enum eHand {
		xhLeft = 0,
		xhRight,
		xhCount
	};

	enum eButtons : uint32_t {
		xbPrimary = 1u,
		xbSecondary = 2u,
		xbMenu = 4u,
		xbThumbstick = 8u
	};

	struct ControllerState {
		Vector2f	thumbstick{ 0.0f, 0.0f };
		float		trigger{ 0.0f };
		float		squeeze{ 0.0f };
		uint32_t	buttons{ 0 };
		bool		isActive{ false };
	};

	virtual ~BaseXRHandler() = default;

	bool Create(const char* appName);

	void Destroy(void)
	noexcept;

	bool CreateSession(int layerWidth, int layerHeight);

	void DestroySession(void)
	noexcept;

	bool BeginFrame(void);

	bool EndFrame(void);

	void PrepareRelease(void)
	noexcept;

	void Recenter(void)
	noexcept;

	void SetViewPitch(float degrees)
	noexcept;

	bool IsAvailable(void) const
	noexcept;

	bool HasSession(void) const
	noexcept;

	void EnableSession(bool enable)
	noexcept;

	bool SessionEnabled(void) const
	noexcept;

	bool DeviceCreated(void) const
	noexcept;

	bool SessionStopped(void) const
	noexcept;

	bool SystemAvailable(void)
	noexcept;

	bool HeadsetReady(void)
	noexcept;

	bool WaitUntilRunning(int timeout)
	noexcept;

	bool IsRunning(void) const
	noexcept;

	bool IsFocused(void) const
	noexcept;

	bool ShouldRender(void) const
	noexcept;

	double DisplayPeriod(void) const
	noexcept;

	bool InstanceLost(void) const
	noexcept;

	int EyeWidth(void) const
	noexcept;

	int EyeHeight(void) const
	noexcept;

	Matrix4f EyeProjection(int view, float zNear, float zFar) const
	noexcept;

	Matrix4f EyeFromHead(int view) const
	noexcept;

	Matrix4f HeadOrientation(void) const
	noexcept;

	Vector3f HeadPosition(void) const
	noexcept;

	void EyeTangents(int view, float& left, float& right, float& up, float& down) const
	noexcept;

	RenderTarget* EyeTarget(void)
	noexcept;

	RenderTarget* DepthTarget(void)
	noexcept;

	RenderTarget* LayerTarget(void)
	noexcept;

	void SetEyesRendered(void)
	noexcept;

	bool EyesRendered(void) const
	noexcept;

	void SetLayer(float distance, float width, bool visible, bool curved, float radius)
	noexcept;

	bool HasCurvedLayer(void) const
	noexcept;

	void SetLayerFovScale(float scale)
	noexcept;

	bool SubmitDepth(Texture* sceneDepth, float zNear, float zFar);

	bool DepthSubmitted(void) const
	noexcept;

	bool RenderLayer(Texture* source, bool bRotate, bool bFlipVertically);

	void WindowFit(int sourceWidth, int sourceHeight, int& x, int& y, int& width, int& height) const
	noexcept;

	bool RenderMirror(bool bFlipVertically, int view);

	bool RenderLayerMirror(Texture* source, bool bFlipVertically, int view);

	void SetTestPattern(bool enable)
	noexcept;

	bool HasDepthLayer(void) const
	noexcept;

	bool HasVisibilityMask(void) const
	noexcept;

	bool VisibilityMask(int view, const AutoArray<Vector2f>*& vertices, const AutoArray<uint32_t>*& indices) const
	noexcept;

	uint32_t VisibilityMaskVersion(void) const
	noexcept;

	const ControllerState& Controller(int hand) const
	noexcept;

#if LOG_FRAME_METRICS
	void EnablePerformanceMetrics(bool enable)
	noexcept;

	void LogPerformanceMetrics(void)
	noexcept;
#endif

protected:
	XRState* m_state{ nullptr };

	void SetDeviceCreated(void)
	noexcept;

	virtual const char* GraphicsExtension(void) const
	noexcept = 0;

	virtual bool LoadGraphics(String& info) = 0;

	virtual const void* GraphicsBinding(void)
	noexcept = 0;

	virtual void SwapchainFormats(const int64_t*& colorFormats, int& colorFormatCount, const int64_t*& depthFormats, int& depthFormatCount) const
	noexcept = 0;

	virtual bool CreateSwapchainTargets(XRSwapchain& swapchain, uint32_t imageCount, bool isDepth, const char* name) = 0;

	virtual bool BackBufferIsSRGB(void) const
	noexcept = 0;

private:
	bool CreateSwapchain(XRSwapchain& swapchain, int64_t format, uint64_t usage, int width, int height, int layers, bool isDepth, const char* name);

	bool RenderLayerToEyes(Texture* source, bool bFlipVertically);
};

// =================================================================================================
