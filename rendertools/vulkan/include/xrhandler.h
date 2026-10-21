#pragma once

#include "vkframework.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "string.hpp"
#include "vector.hpp"
#include "matrix.hpp"
#include "rendertypes.h"

class RenderTarget;
class Texture;
struct XRState;

// =================================================================================================

class GfxXRHandler : public BaseSingleton<GfxXRHandler> {
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

	GfxXRHandler();

	~GfxXRHandler();

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

	bool IsRunning(void) const
	noexcept;

	bool IsFocused(void) const
	noexcept;

	bool ShouldRender(void) const
	noexcept;

	bool ExitRequested(void) const
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

	void SetLayer(float distance, float width, bool visible)
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

	void EnablePerformanceMetrics(bool enable)
	noexcept;

	void LogPerformanceMetrics(void)
	noexcept;

	bool CreateVulkanInstance(const VkInstanceCreateInfo& info, VkInstance& instance)
	noexcept;

	bool GetVulkanPhysicalDevice(VkInstance instance, VkPhysicalDevice& physicalDevice)
	noexcept;

	bool CreateVulkanDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo& info, VkDevice& device)
	noexcept;

private:
	XRState* m_state{ nullptr };
};

#define xrHandler GfxXRHandler::Instance()

// =================================================================================================
