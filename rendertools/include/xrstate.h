#pragma once

#include <chrono>
#include <cstdint>

#include <openxr/openxr.h>

#include "array.hpp"
#include "vector.hpp"
#include "rendertypes.h"
#include "base_xrhandler.h"

class RenderTarget;

// =================================================================================================

struct XRSwapchain {
	XrSwapchain					handle{ XR_NULL_HANDLE };
	int64_t						format{ 0 };
	int							width{ 0 };
	int							height{ 0 };
	int							layers{ 1 };
	AutoArray<RenderTarget*>	targets;
	uint32_t					imageIndex{ 0 };
	bool						isAcquired{ false };
};


struct XRState {
	XrInstance						instance{ XR_NULL_HANDLE };
	XrSystemId						systemId{ XR_NULL_SYSTEM_ID };
	XrSession						session{ XR_NULL_HANDLE };
	XrSession						probeSession{ XR_NULL_HANDLE };
	XrSessionState					sessionState{ XR_SESSION_STATE_UNKNOWN };
	bool							isRunning{ false };
	bool							instanceLost{ false };
	bool							sessionStopped{ false };
	bool							recenterPending{ false };
	float							appliedViewPitch{ 0.0f };
	bool							localChangePending{ false };
	XrTime							localChangeTime{ 0 };
	XrPosef							localChange{ { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };
	XrPosef							appPose{ { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };
#if LOG_FRAME_METRICS
	uint32_t						frameNumber{ 0 };
	XrTime							lastDisplayTime{ 0 };
	float							lastYaw{ 0.0f };
	int								frameLogCount{ 0 };
	double							waitTime{ 0.0 };
	std::chrono::steady_clock::time_point	frameStart{};
#endif
	XrEnvironmentBlendMode			blendMode{ XR_ENVIRONMENT_BLEND_MODE_OPAQUE };
	XrSpace							localSpace{ XR_NULL_HANDLE };
	XrSpace							viewSpace{ XR_NULL_HANDLE };
	XrSpace							appSpace{ XR_NULL_HANDLE };
	XrViewConfigurationView			configViews[kMaxRenderViews]{};
	XrView							views[kMaxRenderViews]{};
	XrPosef							headPose{};
	XrFrameState					frameState{};
	bool							frameBegun{ false };
	bool							eyesRendered{ false };
	bool							layerRendered{ false };
	bool							layerInEyes{ false };
	bool							hasDepthExtension{ false };
	bool							hasVisibilityMaskExtension{ false };
#if LOG_FRAME_METRICS
	bool							hasPerformanceMetricsExtension{ false };
#endif
	bool							hasCylinderExtension{ false };
#if LOG_FRAME_METRICS
	bool							performanceMetricsEnabled{ false };
	XrTime							metricsTime{ 0 };
#endif
	XRSwapchain						color;
	XRSwapchain						depth;
	XRSwapchain						layer;
	XrRect2Di						layerRect{};
	float							layerDistance{ 1.0f };
	float							layerWidth{ 1.0f };
	bool							layerVisible{ false };
	bool							layerCurved{ false };
	float							layerRadius{ 1.0f };
	float							layerFovScale{ 1.0f };
	float							zNear{ 0.0f };
	float							zFar{ 0.0f };
	bool							depthSubmitted{ false };
	AutoArray<Vector2f>				maskVertices[kMaxRenderViews];
	AutoArray<uint32_t>				maskIndices[kMaxRenderViews];
	uint32_t						maskVersion{ 0 };
	XrActionSet						actionSet{ XR_NULL_HANDLE };
	XrAction						thumbstickAction{ XR_NULL_HANDLE };
	XrAction						triggerAction{ XR_NULL_HANDLE };
	XrAction						squeezeAction{ XR_NULL_HANDLE };
	XrAction						primaryAction{ XR_NULL_HANDLE };
	XrAction						secondaryAction{ XR_NULL_HANDLE };
	XrAction						menuAction{ XR_NULL_HANDLE };
	XrAction						thumbclickAction{ XR_NULL_HANDLE };
	XrPath							handPaths[BaseXRHandler::xhCount]{ XR_NULL_PATH, XR_NULL_PATH };
	BaseXRHandler::ControllerState	controllers[BaseXRHandler::xhCount];
#if LOG_FRAME_METRICS
	AutoArray<XrPath>				metricPaths;
#endif

	PFN_xrGetVisibilityMaskKHR							pfnGetVisibilityMask{ nullptr };
#if LOG_FRAME_METRICS
	PFN_xrEnumeratePerformanceMetricsCounterPathsMETA	pfnEnumeratePerformanceMetricsCounterPaths{ nullptr };
	PFN_xrSetPerformanceMetricsStateMETA				pfnSetPerformanceMetricsState{ nullptr };
	PFN_xrQueryPerformanceMetricsCounterMETA			pfnQueryPerformanceMetricsCounter{ nullptr };
#endif
};

// =================================================================================================

bool XrSucceeded(XrInstance instance, XrResult result, const char* what)
noexcept;


template <typename PFN_T>
inline bool LoadXrFunction(XrInstance instance, const char* name, PFN_T& function)
noexcept
{
	function = nullptr;
	return XR_SUCCEEDED(xrGetInstanceProcAddr(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&function))) and (function != nullptr);
}

// =================================================================================================
