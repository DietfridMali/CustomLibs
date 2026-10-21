#define NOMINMAX

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "vkframework.h"

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "xrhandler.h"
#include "vkcontext.h"
#include "gfxrenderer.h"
#include "rendertarget.h"
#include "commandlist.h"
#include "base_shaderhandler.h"
#include "base_displayhandler.h"
#include "loghandler.h"

#ifndef LINUX
#pragma comment(lib, "openxr_loader.lib")
#endif

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
	XrSessionState					sessionState{ XR_SESSION_STATE_UNKNOWN };
	bool							isRunning{ false };
	bool							exitRequested{ false };
	bool							sessionStopped{ false };
	bool							recenterPending{ false };
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
	bool							hasDepthExtension{ false };
	bool							hasVisibilityMaskExtension{ false };
	bool							hasPerformanceMetricsExtension{ false };
	bool							performanceMetricsEnabled{ false };
	XRSwapchain						color;
	XRSwapchain						depth;
	XRSwapchain						layer;
	float							layerDistance{ 1.0f };
	float							layerWidth{ 1.0f };
	bool							layerVisible{ false };
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
	XrPath							handPaths[GfxXRHandler::xhCount]{ XR_NULL_PATH, XR_NULL_PATH };
	GfxXRHandler::ControllerState	controllers[GfxXRHandler::xhCount];
	AutoArray<XrPath>				metricPaths;

	PFN_xrGetVulkanGraphicsRequirements2KHR				pfnGetVulkanGraphicsRequirements2{ nullptr };
	PFN_xrCreateVulkanInstanceKHR						pfnCreateVulkanInstance{ nullptr };
	PFN_xrCreateVulkanDeviceKHR							pfnCreateVulkanDevice{ nullptr };
	PFN_xrGetVulkanGraphicsDevice2KHR					pfnGetVulkanGraphicsDevice2{ nullptr };
	PFN_xrGetVisibilityMaskKHR							pfnGetVisibilityMask{ nullptr };
	PFN_xrEnumeratePerformanceMetricsCounterPathsMETA	pfnEnumeratePerformanceMetricsCounterPaths{ nullptr };
	PFN_xrSetPerformanceMetricsStateMETA				pfnSetPerformanceMetricsState{ nullptr };
	PFN_xrQueryPerformanceMetricsCounterMETA			pfnQueryPerformanceMetricsCounter{ nullptr };
};

// =================================================================================================

static const XrPosef kIdentityPose{ { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };

static bool xrSessionEnabled = true;
static bool xrDeviceCreated = false;
static bool xrTestPattern = false;

static const GfxXRHandler::ControllerState kNoController{};


static bool XrSucceeded(XrInstance instance, XrResult result, const char* what)
noexcept
{
	if (XR_SUCCEEDED(result))
		return true;

	char name[XR_MAX_RESULT_STRING_SIZE]{};

	if ((instance == XR_NULL_HANDLE) or XR_FAILED(xrResultToString(instance, result, name)))
		snprintf(name, sizeof(name), "%d", int(result));
	logHandler.Print("OpenXR: %s failed (%s)\n", what, name);
	return false;
}


template <typename PFN_T>
static bool LoadXrFunction(XrInstance instance, const char* name, PFN_T& function)
noexcept
{
	function = nullptr;
	return XR_SUCCEEDED(xrGetInstanceProcAddr(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&function))) and (function != nullptr);
}


static void RecommendedEyeSize(const XRState& s, int& width, int& height)
noexcept
{
	width = 0;
	height = 0;
	for (int i = 0; i < kMaxRenderViews; ++i) {
		width = std::max(width, int(s.configViews[i].recommendedImageRectWidth));
		height = std::max(height, int(s.configViews[i].recommendedImageRectHeight));
	}
}


static glm::mat4 PoseMatrix(const XrPosef& pose)
noexcept
{
	glm::quat orientation(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);

	return glm::translate(glm::mat4(1.0f), glm::vec3(pose.position.x, pose.position.y, pose.position.z)) * glm::mat4_cast(orientation);
}


static bool HasExtension(const AutoArray<XrExtensionProperties>& extensions, const char* name)
noexcept
{
	for (int i = 0; i < extensions.Length(); ++i) {
		if (std::strcmp(extensions[i].extensionName, name) == 0)
			return true;
	}
	return false;
}


static int64_t SelectFormat(const AutoArray<int64_t>& available, const int64_t* preferred, int preferredCount, bool runtimeOrder)
noexcept
{
	if (runtimeOrder) {
		for (int i = 0; i < available.Length(); ++i) {
			for (int j = 0; j < preferredCount; ++j) {
				if (available[i] == preferred[j])
					return available[i];
			}
		}
		return 0;
	}
	for (int j = 0; j < preferredCount; ++j) {
		for (int i = 0; i < available.Length(); ++i) {
			if (available[i] == preferred[j])
				return available[i];
		}
	}
	return 0;
}


static void DestroySwapchain(XRSwapchain& swapchain)
noexcept
{
	for (int i = 0; i < swapchain.targets.Length(); ++i)
		delete swapchain.targets[i];
	swapchain.targets.Reset();
	if (swapchain.handle != XR_NULL_HANDLE)
		xrDestroySwapchain(swapchain.handle);
	swapchain.handle = XR_NULL_HANDLE;
	swapchain.isAcquired = false;
}


static bool CreateSwapchain(XRState& s, XRSwapchain& swapchain, int64_t format, XrSwapchainUsageFlags usage, int width, int height,
							int layers, bool isDepth, const char* name)
{
	XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };

	info.usageFlags = usage;
	info.format = format;
	info.sampleCount = 1;
	info.width = uint32_t(width);
	info.height = uint32_t(height);
	info.faceCount = 1;
	info.arraySize = uint32_t(layers);
	info.mipCount = 1;
	if (not XrSucceeded(s.instance, xrCreateSwapchain(s.session, &info, &swapchain.handle), "xrCreateSwapchain"))
		return false;
	swapchain.format = format;
	swapchain.width = width;
	swapchain.height = height;
	swapchain.layers = layers;

	uint32_t imageCount = 0;

	if (not XrSucceeded(s.instance, xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr), "xrEnumerateSwapchainImages"))
		return false;

	AutoArray<XrSwapchainImageVulkan2KHR> images;

	images.Resize(int32_t(imageCount));
	for (int i = 0; i < images.Length(); ++i)
		images[i] = XrSwapchainImageVulkan2KHR{ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
	if (not XrSucceeded(s.instance,
						xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount,
												   reinterpret_cast<XrSwapchainImageBaseHeader*>(images.Data())),
						"xrEnumerateSwapchainImages"))
		return false;

	swapchain.targets.Resize(int32_t(imageCount));
	for (int i = 0; i < swapchain.targets.Length(); ++i)
		swapchain.targets[i] = nullptr;
	for (uint32_t i = 0; i < imageCount; ++i) {
		RenderTarget* target = new (std::nothrow) RenderTarget();

		if (not target)
			return false;
		swapchain.targets[int32_t(i)] = target;

		bool ok = isDepth ? target->CreateExternal(width, height, String(name), layers, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED, images[int32_t(i)].image,
												   VkFormat(format))
						  : target->CreateExternal(width, height, String(name), layers, images[int32_t(i)].image, VkFormat(format));

		if (not ok) {
			logHandler.Print("OpenXR: swapchain image %u of '%s' could not be wrapped\n", i, name);
			return false;
		}
	}
	return true;
}


static bool AcquireSwapchainImage(XRState& s, XRSwapchain& swapchain, const char* what)
noexcept
{
	swapchain.isAcquired = false;
	if (swapchain.handle == XR_NULL_HANDLE)
		return false;

	XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };

	if (not XrSucceeded(s.instance, xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &swapchain.imageIndex), what))
		return false;

	XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };

	waitInfo.timeout = XR_INFINITE_DURATION;
	if (not XrSucceeded(s.instance, xrWaitSwapchainImage(swapchain.handle, &waitInfo), what)) {
		XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

		xrReleaseSwapchainImage(swapchain.handle, &releaseInfo);
		return false;
	}
	swapchain.isAcquired = true;
	return true;
}


static void ReleaseSwapchainImage(XRState& s, XRSwapchain& swapchain)
noexcept
{
	if (not swapchain.isAcquired)
		return;

	XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

	XrSucceeded(s.instance, xrReleaseSwapchainImage(swapchain.handle, &releaseInfo), "xrReleaseSwapchainImage");
	swapchain.isAcquired = false;
}


static bool CreateAppSpace(XRState& s, const XrPosef& pose)
noexcept
{
	XrReferenceSpaceCreateInfo info{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	XrSpace space = XR_NULL_HANDLE;

	info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	info.poseInReferenceSpace = pose;
	if (not XrSucceeded(s.instance, xrCreateReferenceSpace(s.session, &info, &space), "xrCreateReferenceSpace (app)"))
		return false;
	if (s.appSpace != XR_NULL_HANDLE)
		xrDestroySpace(s.appSpace);
	s.appSpace = space;
	return true;
}


static bool RecenterAppSpace(XRState& s)
noexcept
{
	XrSpaceLocation	location{ XR_TYPE_SPACE_LOCATION };
	XrTime			time = s.frameState.predictedDisplayTime;

	if ((time == 0) or XR_FAILED(xrLocateSpace(s.viewSpace, s.localSpace, time, &location)))
		return false;
	if ((location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0)
		return false;

	XrPosef pose = kIdentityPose;

	pose.orientation = location.pose.orientation;
	if (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
		pose.position = location.pose.position;
	if (not CreateAppSpace(s, pose))
		return false;
	logHandler.Print("OpenXR: reference pose set (orientation %.3f %.3f %.3f %.3f)\n", pose.orientation.x, pose.orientation.y, pose.orientation.z,
					 pose.orientation.w);
	return true;
}


static void FillTestPattern(XRState& s)
{
	RenderTarget*	eye = s.color.targets[int32_t(s.color.imageIndex)];
	RGBAColor		clearColor = gfxStates.GetClearColor();

	if (not (eye and eye->Activate({ .clear = false })))
		return;
	gfxStates.SetClearColor(1.0f, 0.0f, 1.0f, 1.0f);
	gfxStates.ClearColorBuffers();
	gfxStates.SetClearColor(clearColor);
	eye->Deactivate();
	s.eyesRendered = true;
}


static void UpdateVisibilityMasks(XRState& s)
noexcept
{
	if (not (s.hasVisibilityMaskExtension and s.pfnGetVisibilityMask and (s.session != XR_NULL_HANDLE)))
		return;
	for (uint32_t view = 0; view < uint32_t(kMaxRenderViews); ++view) {
		XrVisibilityMaskKHR mask{ XR_TYPE_VISIBILITY_MASK_KHR };

		s.maskVertices[view].Reset();
		s.maskIndices[view].Reset();
		if (XR_FAILED(s.pfnGetVisibilityMask(s.session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view,
											  XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &mask)))
			continue;
		if ((mask.vertexCountOutput == 0) or (mask.indexCountOutput == 0))
			continue;

		AutoArray<XrVector2f> vertices;

		vertices.Resize(int32_t(mask.vertexCountOutput));
		s.maskIndices[view].Resize(int32_t(mask.indexCountOutput));
		mask.vertexCapacityInput = mask.vertexCountOutput;
		mask.vertices = vertices.Data();
		mask.indexCapacityInput = mask.indexCountOutput;
		mask.indices = s.maskIndices[view].Data();
		if (XR_FAILED(s.pfnGetVisibilityMask(s.session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view,
											  XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &mask))) {
			s.maskIndices[view].Reset();
			continue;
		}
		s.maskVertices[view].Resize(int32_t(mask.vertexCountOutput));
		for (uint32_t i = 0; i < mask.vertexCountOutput; ++i)
			s.maskVertices[view][int32_t(i)] = Vector2f{ vertices[int32_t(i)].x, vertices[int32_t(i)].y };
	}
	++s.maskVersion;
}


static XrPath StringToPath(XRState& s, const char* path)
noexcept
{
	XrPath result = XR_NULL_PATH;

	if (XR_FAILED(xrStringToPath(s.instance, path, &result)))
		return XR_NULL_PATH;
	return result;
}


static bool CreateAction(XRState& s, XrAction& action, const char* name, const char* localizedName, XrActionType type)
noexcept
{
	XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };

	snprintf(info.actionName, sizeof(info.actionName), "%s", name);
	snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", localizedName);
	info.actionType = type;
	info.countSubactionPaths = uint32_t(GfxXRHandler::xhCount);
	info.subactionPaths = s.handPaths;
	return XrSucceeded(s.instance, xrCreateAction(s.actionSet, &info, &action), "xrCreateAction");
}


struct XRBinding {
	XrAction*	action;
	const char*	path;
};


static void SuggestBindings(XRState& s, const char* profile, const XRBinding* bindings, int bindingCount)
noexcept
{
	AutoArray<XrActionSuggestedBinding> suggested;

	for (int i = 0; i < bindingCount; ++i) {
		XrPath path = StringToPath(s, bindings[i].path);

		if (path == XR_NULL_PATH)
			continue;

		XrActionSuggestedBinding* binding = suggested.Append();

		if (binding) {
			binding->action = *bindings[i].action;
			binding->binding = path;
		}
	}

	XrInteractionProfileSuggestedBinding info{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };

	info.interactionProfile = StringToPath(s, profile);
	info.countSuggestedBindings = uint32_t(suggested.Length());
	info.suggestedBindings = suggested.Data();
	if ((info.interactionProfile == XR_NULL_PATH) or (info.countSuggestedBindings == 0))
		return;
	if (XR_FAILED(xrSuggestInteractionProfileBindings(s.instance, &info)))
		logHandler.Print("OpenXR: bindings for '%s' were not accepted\n", profile);
}


static bool CreateActions(XRState& s)
noexcept
{
	XrActionSetCreateInfo setInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };

	snprintf(setInfo.actionSetName, sizeof(setInfo.actionSetName), "%s", "gameplay");
	snprintf(setInfo.localizedActionSetName, sizeof(setInfo.localizedActionSetName), "%s", "Gameplay");
	setInfo.priority = 0;
	if (not XrSucceeded(s.instance, xrCreateActionSet(s.instance, &setInfo, &s.actionSet), "xrCreateActionSet"))
		return false;
	s.handPaths[GfxXRHandler::xhLeft] = StringToPath(s, "/user/hand/left");
	s.handPaths[GfxXRHandler::xhRight] = StringToPath(s, "/user/hand/right");
	if (not (CreateAction(s, s.thumbstickAction, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT) and
			 CreateAction(s, s.triggerAction, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT) and
			 CreateAction(s, s.squeezeAction, "squeeze", "Squeeze", XR_ACTION_TYPE_FLOAT_INPUT) and
			 CreateAction(s, s.primaryAction, "primary", "Primary button", XR_ACTION_TYPE_BOOLEAN_INPUT) and
			 CreateAction(s, s.secondaryAction, "secondary", "Secondary button", XR_ACTION_TYPE_BOOLEAN_INPUT) and
			 CreateAction(s, s.menuAction, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT) and
			 CreateAction(s, s.thumbclickAction, "thumbclick", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT)))
		return false;

	const XRBinding simpleBindings[] = {
		{ &s.triggerAction, "/user/hand/left/input/select/click" },
		{ &s.triggerAction, "/user/hand/right/input/select/click" },
		{ &s.menuAction, "/user/hand/left/input/menu/click" },
		{ &s.menuAction, "/user/hand/right/input/menu/click" },
	};
	const XRBinding touchBindings[] = {
		{ &s.thumbstickAction, "/user/hand/left/input/thumbstick" },
		{ &s.thumbstickAction, "/user/hand/right/input/thumbstick" },
		{ &s.triggerAction, "/user/hand/left/input/trigger/value" },
		{ &s.triggerAction, "/user/hand/right/input/trigger/value" },
		{ &s.squeezeAction, "/user/hand/left/input/squeeze/value" },
		{ &s.squeezeAction, "/user/hand/right/input/squeeze/value" },
		{ &s.primaryAction, "/user/hand/left/input/x/click" },
		{ &s.primaryAction, "/user/hand/right/input/a/click" },
		{ &s.secondaryAction, "/user/hand/left/input/y/click" },
		{ &s.secondaryAction, "/user/hand/right/input/b/click" },
		{ &s.menuAction, "/user/hand/left/input/menu/click" },
		{ &s.thumbclickAction, "/user/hand/left/input/thumbstick/click" },
		{ &s.thumbclickAction, "/user/hand/right/input/thumbstick/click" },
	};
	const XRBinding indexBindings[] = {
		{ &s.thumbstickAction, "/user/hand/left/input/thumbstick" },
		{ &s.thumbstickAction, "/user/hand/right/input/thumbstick" },
		{ &s.triggerAction, "/user/hand/left/input/trigger/value" },
		{ &s.triggerAction, "/user/hand/right/input/trigger/value" },
		{ &s.squeezeAction, "/user/hand/left/input/squeeze/value" },
		{ &s.squeezeAction, "/user/hand/right/input/squeeze/value" },
		{ &s.primaryAction, "/user/hand/left/input/a/click" },
		{ &s.primaryAction, "/user/hand/right/input/a/click" },
		{ &s.secondaryAction, "/user/hand/left/input/b/click" },
		{ &s.secondaryAction, "/user/hand/right/input/b/click" },
		{ &s.thumbclickAction, "/user/hand/left/input/thumbstick/click" },
		{ &s.thumbclickAction, "/user/hand/right/input/thumbstick/click" },
	};
	const XRBinding viveBindings[] = {
		{ &s.thumbstickAction, "/user/hand/left/input/trackpad" },
		{ &s.thumbstickAction, "/user/hand/right/input/trackpad" },
		{ &s.triggerAction, "/user/hand/left/input/trigger/value" },
		{ &s.triggerAction, "/user/hand/right/input/trigger/value" },
		{ &s.squeezeAction, "/user/hand/left/input/squeeze/click" },
		{ &s.squeezeAction, "/user/hand/right/input/squeeze/click" },
		{ &s.menuAction, "/user/hand/left/input/menu/click" },
		{ &s.menuAction, "/user/hand/right/input/menu/click" },
		{ &s.thumbclickAction, "/user/hand/left/input/trackpad/click" },
		{ &s.thumbclickAction, "/user/hand/right/input/trackpad/click" },
	};
	const XRBinding motionBindings[] = {
		{ &s.thumbstickAction, "/user/hand/left/input/thumbstick" },
		{ &s.thumbstickAction, "/user/hand/right/input/thumbstick" },
		{ &s.triggerAction, "/user/hand/left/input/trigger/value" },
		{ &s.triggerAction, "/user/hand/right/input/trigger/value" },
		{ &s.squeezeAction, "/user/hand/left/input/squeeze/click" },
		{ &s.squeezeAction, "/user/hand/right/input/squeeze/click" },
		{ &s.primaryAction, "/user/hand/left/input/trackpad/click" },
		{ &s.primaryAction, "/user/hand/right/input/trackpad/click" },
		{ &s.menuAction, "/user/hand/left/input/menu/click" },
		{ &s.menuAction, "/user/hand/right/input/menu/click" },
		{ &s.thumbclickAction, "/user/hand/left/input/thumbstick/click" },
		{ &s.thumbclickAction, "/user/hand/right/input/thumbstick/click" },
	};

	SuggestBindings(s, "/interaction_profiles/khr/simple_controller", simpleBindings, int(sizeof(simpleBindings) / sizeof(simpleBindings[0])));
	SuggestBindings(s, "/interaction_profiles/oculus/touch_controller", touchBindings, int(sizeof(touchBindings) / sizeof(touchBindings[0])));
	SuggestBindings(s, "/interaction_profiles/valve/index_controller", indexBindings, int(sizeof(indexBindings) / sizeof(indexBindings[0])));
	SuggestBindings(s, "/interaction_profiles/htc/vive_controller", viveBindings, int(sizeof(viveBindings) / sizeof(viveBindings[0])));
	SuggestBindings(s, "/interaction_profiles/microsoft/motion_controller", motionBindings,
					int(sizeof(motionBindings) / sizeof(motionBindings[0])));

	XrSessionActionSetsAttachInfo attachInfo{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };

	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &s.actionSet;
	return XrSucceeded(s.instance, xrAttachSessionActionSets(s.session, &attachInfo), "xrAttachSessionActionSets");
}


static bool BooleanAction(XRState& s, XrAction action, XrPath hand)
noexcept
{
	XrActionStateGetInfo	info{ XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateBoolean	state{ XR_TYPE_ACTION_STATE_BOOLEAN };

	info.action = action;
	info.subactionPath = hand;
	return XR_SUCCEEDED(xrGetActionStateBoolean(s.session, &info, &state)) and state.isActive and state.currentState;
}


static void SyncActions(XRState& s)
noexcept
{
	for (int hand = 0; hand < GfxXRHandler::xhCount; ++hand)
		s.controllers[hand] = GfxXRHandler::ControllerState{};
	if ((s.actionSet == XR_NULL_HANDLE) or (s.sessionState != XR_SESSION_STATE_FOCUSED))
		return;

	XrActiveActionSet	activeSet{ s.actionSet, XR_NULL_PATH };
	XrActionsSyncInfo	syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };

	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeSet;
	if (XR_FAILED(xrSyncActions(s.session, &syncInfo)))
		return;
	for (int hand = 0; hand < GfxXRHandler::xhCount; ++hand) {
		GfxXRHandler::ControllerState&	controller = s.controllers[hand];
		XrActionStateGetInfo			info{ XR_TYPE_ACTION_STATE_GET_INFO };
		XrActionStateVector2f			stick{ XR_TYPE_ACTION_STATE_VECTOR2F };
		XrActionStateFloat				value{ XR_TYPE_ACTION_STATE_FLOAT };

		info.subactionPath = s.handPaths[hand];
		info.action = s.thumbstickAction;
		if (XR_SUCCEEDED(xrGetActionStateVector2f(s.session, &info, &stick)) and stick.isActive) {
			controller.thumbstick = Vector2f{ stick.currentState.x, stick.currentState.y };
			controller.isActive = true;
		}
		info.action = s.triggerAction;
		if (XR_SUCCEEDED(xrGetActionStateFloat(s.session, &info, &value)) and value.isActive) {
			controller.trigger = value.currentState;
			controller.isActive = true;
		}
		value = XrActionStateFloat{ XR_TYPE_ACTION_STATE_FLOAT };
		info.action = s.squeezeAction;
		if (XR_SUCCEEDED(xrGetActionStateFloat(s.session, &info, &value)) and value.isActive) {
			controller.squeeze = value.currentState;
			controller.isActive = true;
		}
		if (BooleanAction(s, s.primaryAction, s.handPaths[hand]))
			controller.buttons |= GfxXRHandler::xbPrimary;
		if (BooleanAction(s, s.secondaryAction, s.handPaths[hand]))
			controller.buttons |= GfxXRHandler::xbSecondary;
		if (BooleanAction(s, s.menuAction, s.handPaths[hand]))
			controller.buttons |= GfxXRHandler::xbMenu;
		if (BooleanAction(s, s.thumbclickAction, s.handPaths[hand]))
			controller.buttons |= GfxXRHandler::xbThumbstick;
	}
}


static const char* SessionStateName(XrSessionState state)
noexcept
{
	switch (state) {
		case XR_SESSION_STATE_IDLE:
			return "idle";
		case XR_SESSION_STATE_READY:
			return "ready";
		case XR_SESSION_STATE_SYNCHRONIZED:
			return "synchronized";
		case XR_SESSION_STATE_VISIBLE:
			return "visible";
		case XR_SESSION_STATE_FOCUSED:
			return "focused";
		case XR_SESSION_STATE_STOPPING:
			return "stopping";
		case XR_SESSION_STATE_LOSS_PENDING:
			return "loss pending";
		case XR_SESSION_STATE_EXITING:
			return "exiting";
		default:
			return "unknown";
	}
}


static void PollEvents(XRState& s)
noexcept
{
	XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };

	while (xrPollEvent(s.instance, &event) == XR_SUCCESS) {
		switch (event.type) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const XrEventDataSessionStateChanged& changed = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);

				logHandler.Print("OpenXR: session state %s\n", SessionStateName(changed.state));
				s.sessionState = changed.state;
				if (changed.state == XR_SESSION_STATE_READY) {
					XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO };

					beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					s.isRunning = XrSucceeded(s.instance, xrBeginSession(s.session, &beginInfo), "xrBeginSession");
				}
				else if (changed.state == XR_SESSION_STATE_STOPPING) {
					XrSucceeded(s.instance, xrEndSession(s.session), "xrEndSession");
					s.isRunning = false;
					s.sessionStopped = true;
				}
				else if ((changed.state == XR_SESSION_STATE_EXITING) or (changed.state == XR_SESSION_STATE_LOSS_PENDING)) {
					s.isRunning = false;
					s.exitRequested = true;
					s.sessionStopped = true;
				}
				break;
			}

			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				logHandler.Print("OpenXR: instance loss pending\n");
				s.isRunning = false;
				s.exitRequested = true;
				s.sessionStopped = true;
				break;

			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				const XrEventDataReferenceSpaceChangePending& changed =
					*reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);

				if (changed.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL)
					s.recenterPending = true;
				break;
			}

			case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR:
				UpdateVisibilityMasks(s);
				break;

			default:
				break;
		}
		event = XrEventDataBuffer{ XR_TYPE_EVENT_DATA_BUFFER };
	}
}

// =================================================================================================

GfxXRHandler::GfxXRHandler()
{
}


GfxXRHandler::~GfxXRHandler()
{
	Destroy();
}


bool GfxXRHandler::Create(const char* appName)
{
	if (IsAvailable())
		return true;
	if (not (m_state or (m_state = new (std::nothrow) XRState())))
		return false;

	XRState&	s = *m_state;
	uint32_t	extensionCount = 0;

	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr))) {
		logHandler.Print("OpenXR: no runtime found\n");
		return false;
	}

	AutoArray<XrExtensionProperties> extensions;

	extensions.Resize(int32_t(extensionCount));
	for (int i = 0; i < extensions.Length(); ++i)
		extensions[i] = XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES };
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.Data()))) {
		logHandler.Print("OpenXR: the runtime's extensions cannot be read\n");
		return false;
	}
	if (not HasExtension(extensions, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) {
		logHandler.Print("OpenXR: the runtime has no %s\n", XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
		return false;
	}
	s.hasDepthExtension = HasExtension(extensions, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
	s.hasVisibilityMaskExtension = HasExtension(extensions, XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
	s.hasPerformanceMetricsExtension = HasExtension(extensions, XR_META_PERFORMANCE_METRICS_EXTENSION_NAME);

	const char*	enabledExtensions[4]{};
	uint32_t	enabledCount = 0;

	enabledExtensions[enabledCount++] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
	if (s.hasDepthExtension)
		enabledExtensions[enabledCount++] = XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME;
	if (s.hasVisibilityMaskExtension)
		enabledExtensions[enabledCount++] = XR_KHR_VISIBILITY_MASK_EXTENSION_NAME;
	if (s.hasPerformanceMetricsExtension)
		enabledExtensions[enabledCount++] = XR_META_PERFORMANCE_METRICS_EXTENSION_NAME;

	XrInstanceCreateInfo instanceInfo{ XR_TYPE_INSTANCE_CREATE_INFO };

	snprintf(instanceInfo.applicationInfo.applicationName, sizeof(instanceInfo.applicationInfo.applicationName), "%s",
			 (appName and *appName) ? appName : "rendertools");
	snprintf(instanceInfo.applicationInfo.engineName, sizeof(instanceInfo.applicationInfo.engineName), "%s", "rendertools");
	instanceInfo.applicationInfo.applicationVersion = 1;
	instanceInfo.applicationInfo.engineVersion = 1;
	instanceInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	instanceInfo.enabledExtensionCount = enabledCount;
	instanceInfo.enabledExtensionNames = enabledExtensions;
	if (not XrSucceeded(XR_NULL_HANDLE, xrCreateInstance(&instanceInfo, &s.instance), "xrCreateInstance")) {
		s.instance = XR_NULL_HANDLE;
		return false;
	}

	XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };

	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (not XrSucceeded(s.instance, xrGetSystem(s.instance, &systemInfo, &s.systemId), "xrGetSystem (no headset connected?)")) {
		Destroy();
		return false;
	}

	uint32_t viewCount = 0;

	if (not XrSucceeded(s.instance,
						xrEnumerateViewConfigurationViews(s.instance, s.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr),
						"xrEnumerateViewConfigurationViews") or
		(viewCount != uint32_t(kMaxRenderViews))) {
		Destroy();
		return false;
	}
	for (int i = 0; i < kMaxRenderViews; ++i)
		s.configViews[i] = XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW };
	if (not XrSucceeded(s.instance,
						xrEnumerateViewConfigurationViews(s.instance, s.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount,
														  &viewCount, s.configViews),
						"xrEnumerateViewConfigurationViews")) {
		Destroy();
		return false;
	}

	uint32_t blendModeCount = 0;

	if (XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(s.instance, s.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blendModeCount, nullptr)) and
		(blendModeCount > 0)) {
		AutoArray<XrEnvironmentBlendMode> blendModes;

		blendModes.Resize(int32_t(blendModeCount));
		if (XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(s.instance, s.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blendModeCount,
														  &blendModeCount, blendModes.Data()))) {
			s.blendMode = blendModes[0];
			for (uint32_t i = 0; i < blendModeCount; ++i) {
				if (blendModes[int32_t(i)] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
					s.blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
			}
		}
	}

	if (not (LoadXrFunction(s.instance, "xrGetVulkanGraphicsRequirements2KHR", s.pfnGetVulkanGraphicsRequirements2) and
			 LoadXrFunction(s.instance, "xrCreateVulkanInstanceKHR", s.pfnCreateVulkanInstance) and
			 LoadXrFunction(s.instance, "xrCreateVulkanDeviceKHR", s.pfnCreateVulkanDevice) and
			 LoadXrFunction(s.instance, "xrGetVulkanGraphicsDevice2KHR", s.pfnGetVulkanGraphicsDevice2))) {
		logHandler.Print("OpenXR: the Vulkan entry points of the runtime cannot be loaded\n");
		Destroy();
		return false;
	}
	if (s.hasVisibilityMaskExtension and not LoadXrFunction(s.instance, "xrGetVisibilityMaskKHR", s.pfnGetVisibilityMask))
		s.hasVisibilityMaskExtension = false;
	if (s.hasPerformanceMetricsExtension and
		not (LoadXrFunction(s.instance, "xrEnumeratePerformanceMetricsCounterPathsMETA", s.pfnEnumeratePerformanceMetricsCounterPaths) and
			 LoadXrFunction(s.instance, "xrSetPerformanceMetricsStateMETA", s.pfnSetPerformanceMetricsState) and
			 LoadXrFunction(s.instance, "xrQueryPerformanceMetricsCounterMETA", s.pfnQueryPerformanceMetricsCounter)))
		s.hasPerformanceMetricsExtension = false;

	XrGraphicsRequirementsVulkan2KHR requirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };

	if (not XrSucceeded(s.instance, s.pfnGetVulkanGraphicsRequirements2(s.instance, s.systemId, &requirements),
						"xrGetVulkanGraphicsRequirements2KHR")) {
		Destroy();
		return false;
	}

	XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES };
	int eyeWidth = 0;
	int eyeHeight = 0;

	RecommendedEyeSize(s, eyeWidth, eyeHeight);
	if (XR_SUCCEEDED(xrGetSystemProperties(s.instance, s.systemId, &systemProperties)))
		logHandler.Print("OpenXR: %s, eye %d x %d, Vulkan %u.%u .. %u.%u\n", systemProperties.systemName, eyeWidth, eyeHeight,
						 unsigned(XR_VERSION_MAJOR(requirements.minApiVersionSupported)),
						 unsigned(XR_VERSION_MINOR(requirements.minApiVersionSupported)),
						 unsigned(XR_VERSION_MAJOR(requirements.maxApiVersionSupported)),
						 unsigned(XR_VERSION_MINOR(requirements.maxApiVersionSupported)));
	return true;
}


void GfxXRHandler::Destroy(void)
noexcept
{
	if (not m_state)
		return;
	DestroySession();
	if (m_state->instance != XR_NULL_HANDLE)
		xrDestroyInstance(m_state->instance);
	delete m_state;
	m_state = nullptr;
}


bool GfxXRHandler::CreateVulkanInstance(const VkInstanceCreateInfo& info, VkInstance& instance)
noexcept
{
	if (not (IsAvailable() and m_state->pfnCreateVulkanInstance))
		return false;

	XrVulkanInstanceCreateInfoKHR	createInfo{ XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
	VkResult						result = VK_SUCCESS;

	createInfo.systemId = m_state->systemId;
	createInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
	createInfo.vulkanCreateInfo = &info;
	if (not XrSucceeded(m_state->instance, m_state->pfnCreateVulkanInstance(m_state->instance, &createInfo, &instance, &result),
						"xrCreateVulkanInstanceKHR"))
		return false;
	if (result != VK_SUCCESS) {
		logHandler.Print("OpenXR: vkCreateInstance through the runtime failed (%d)\n", int(result));
		return false;
	}
	return true;
}


bool GfxXRHandler::GetVulkanPhysicalDevice(VkInstance instance, VkPhysicalDevice& physicalDevice)
noexcept
{
	if (not (IsAvailable() and m_state->pfnGetVulkanGraphicsDevice2))
		return false;

	XrVulkanGraphicsDeviceGetInfoKHR getInfo{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };

	getInfo.systemId = m_state->systemId;
	getInfo.vulkanInstance = instance;
	return XrSucceeded(m_state->instance, m_state->pfnGetVulkanGraphicsDevice2(m_state->instance, &getInfo, &physicalDevice),
					   "xrGetVulkanGraphicsDevice2KHR") and
		(physicalDevice != VK_NULL_HANDLE);
}


bool GfxXRHandler::CreateVulkanDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo& info, VkDevice& device)
noexcept
{
	if (not (IsAvailable() and m_state->pfnCreateVulkanDevice))
		return false;

	XrVulkanDeviceCreateInfoKHR	createInfo{ XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
	VkResult					result = VK_SUCCESS;

	createInfo.systemId = m_state->systemId;
	createInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
	createInfo.vulkanPhysicalDevice = physicalDevice;
	createInfo.vulkanCreateInfo = &info;
	if (not XrSucceeded(m_state->instance, m_state->pfnCreateVulkanDevice(m_state->instance, &createInfo, &device, &result),
						"xrCreateVulkanDeviceKHR"))
		return false;
	if (result != VK_SUCCESS) {
		logHandler.Print("OpenXR: vkCreateDevice through the runtime failed (%d)\n", int(result));
		return false;
	}
	xrDeviceCreated = true;
	return true;
}


bool GfxXRHandler::CreateSession(int layerWidth, int layerHeight)
{
	if (not IsAvailable())
		return false;
	if (HasSession())
		return true;

	XRState&						s = *m_state;

	s.sessionStopped = false;
	s.recenterPending = true;
	XrGraphicsBindingVulkan2KHR		binding{ XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
	XrSessionCreateInfo				sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };

	binding.instance = vkContext.m_instance;
	binding.physicalDevice = vkContext.PhysicalDevice();
	binding.device = vkContext.Device();
	binding.queueFamilyIndex = vkContext.GraphicsFamily();
	binding.queueIndex = 0;
	sessionInfo.next = &binding;
	sessionInfo.systemId = s.systemId;
	if (not XrSucceeded(s.instance, xrCreateSession(s.instance, &sessionInfo, &s.session), "xrCreateSession")) {
		s.session = XR_NULL_HANDLE;
		return false;
	}

	XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };

	spaceInfo.poseInReferenceSpace = kIdentityPose;
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (not XrSucceeded(s.instance, xrCreateReferenceSpace(s.session, &spaceInfo, &s.localSpace), "xrCreateReferenceSpace (local)")) {
		DestroySession();
		return false;
	}
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if (not XrSucceeded(s.instance, xrCreateReferenceSpace(s.session, &spaceInfo, &s.viewSpace), "xrCreateReferenceSpace (view)") or
		not CreateAppSpace(s, kIdentityPose)) {
		DestroySession();
		return false;
	}

	uint32_t formatCount = 0;

	if (not XrSucceeded(s.instance, xrEnumerateSwapchainFormats(s.session, 0, &formatCount, nullptr), "xrEnumerateSwapchainFormats")) {
		DestroySession();
		return false;
	}

	AutoArray<int64_t> formats;

	formats.Resize(int32_t(formatCount));
	if (not XrSucceeded(s.instance, xrEnumerateSwapchainFormats(s.session, formatCount, &formatCount, formats.Data()),
						"xrEnumerateSwapchainFormats")) {
		DestroySession();
		return false;
	}

	static const int64_t colorFormats[] = { VK_FORMAT_R8G8B8A8_SRGB,	   VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R16G16B16A16_SFLOAT,
											VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
	static const int64_t depthFormats[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D16_UNORM };

	int64_t colorFormat = SelectFormat(formats, colorFormats, int(sizeof(colorFormats) / sizeof(colorFormats[0])), true);
	int64_t depthFormat = SelectFormat(formats, depthFormats, int(sizeof(depthFormats) / sizeof(depthFormats[0])), false);

	if (colorFormat == 0) {
		logHandler.Print("OpenXR: the runtime offers no usable colour swapchain format\n");
		DestroySession();
		return false;
	}

	int eyeWidth = 0;
	int eyeHeight = 0;

	RecommendedEyeSize(s, eyeWidth, eyeHeight);
	if (not CreateSwapchain(s, s.color, colorFormat,
							XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT, eyeWidth,
							eyeHeight, kMaxRenderViews, false, "xr eye buffer")) {
		DestroySession();
		return false;
	}
	if (s.hasDepthExtension and (depthFormat != 0) and
		not CreateSwapchain(s, s.depth, depthFormat, XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, eyeWidth, eyeHeight, kMaxRenderViews,
							true, "xr depth buffer")) {
		DestroySwapchain(s.depth);
		s.hasDepthExtension = false;
	}
	if ((layerWidth > 0) and (layerHeight > 0) and
		not CreateSwapchain(s, s.layer, colorFormat,
							XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT,
							layerWidth, layerHeight, 1, false, "xr layer buffer"))
		DestroySwapchain(s.layer);

	for (int i = 0; i < kMaxRenderViews; ++i) {
		s.views[i] = XrView{ XR_TYPE_VIEW };
		s.views[i].pose = kIdentityPose;
		s.views[i].fov = XrFovf{ -0.8f, 0.8f, 0.8f, -0.8f };
	}
	s.headPose = kIdentityPose;
	if (not CreateActions(s))
		logHandler.Print("OpenXR: controller input is not available\n");
	UpdateVisibilityMasks(s);
	if (s.performanceMetricsEnabled)
		EnablePerformanceMetrics(true);
	logHandler.Print("OpenXR: session created, eye buffers %d x %d (format %lld), depth layer %s, visibility mask %s\n", eyeWidth, eyeHeight,
					 static_cast<long long>(colorFormat), s.hasDepthExtension ? "on" : "off", s.hasVisibilityMaskExtension ? "on" : "off");
	return true;
}


void GfxXRHandler::DestroySession(void)
noexcept
{
	if (not m_state)
		return;

	XRState& s = *m_state;

	DestroySwapchain(s.color);
	DestroySwapchain(s.depth);
	DestroySwapchain(s.layer);
	if (s.actionSet != XR_NULL_HANDLE)
		xrDestroyActionSet(s.actionSet);
	s.actionSet = XR_NULL_HANDLE;
	for (XrSpace* space : { &s.appSpace, &s.viewSpace, &s.localSpace }) {
		if (*space != XR_NULL_HANDLE)
			xrDestroySpace(*space);
		*space = XR_NULL_HANDLE;
	}
	if (s.session != XR_NULL_HANDLE) {
		if (s.isRunning)
			xrEndSession(s.session);
		xrDestroySession(s.session);
		logHandler.Print("OpenXR: session destroyed\n");
	}
	s.session = XR_NULL_HANDLE;
	s.isRunning = false;
	s.frameBegun = false;
	s.sessionState = XR_SESSION_STATE_UNKNOWN;
}


bool GfxXRHandler::BeginFrame(void)
{
	if (not HasSession())
		return false;

	XRState& s = *m_state;

	PollEvents(s);
	if (not s.isRunning)
		return false;

	XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };

	s.frameState = XrFrameState{ XR_TYPE_FRAME_STATE };
	if (not XrSucceeded(s.instance, xrWaitFrame(s.session, &waitInfo, &s.frameState), "xrWaitFrame"))
		return false;

	XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };

	if (not XrSucceeded(s.instance, xrBeginFrame(s.session, &beginInfo), "xrBeginFrame"))
		return false;
	s.frameBegun = true;
	if (s.recenterPending and RecenterAppSpace(s))
		s.recenterPending = false;
	s.eyesRendered = false;
	s.layerRendered = false;
	s.depthSubmitted = false;

	XrViewLocateInfo	locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
	XrViewState			viewState{ XR_TYPE_VIEW_STATE };
	XrView				views[kMaxRenderViews];
	uint32_t			viewCount = 0;

	for (int i = 0; i < kMaxRenderViews; ++i)
		views[i] = XrView{ XR_TYPE_VIEW };
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = s.frameState.predictedDisplayTime;
	locateInfo.space = s.appSpace;
	if (XR_SUCCEEDED(xrLocateViews(s.session, &locateInfo, &viewState, uint32_t(kMaxRenderViews), &viewCount, views)) and
		(viewCount == uint32_t(kMaxRenderViews)) and (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
		for (int i = 0; i < kMaxRenderViews; ++i) {
			s.views[i].fov = views[i].fov;
			s.views[i].pose.orientation = views[i].pose.orientation;
			if (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)
				s.views[i].pose.position = views[i].pose.position;
		}
	}

	XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };

	if (XR_SUCCEEDED(xrLocateSpace(s.viewSpace, s.appSpace, s.frameState.predictedDisplayTime, &location))) {
		if (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
			s.headPose.orientation = location.pose.orientation;
		if (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
			s.headPose.position = location.pose.position;
	}
	SyncActions(s);
	if (s.frameState.shouldRender) {
		AcquireSwapchainImage(s, s.color, "xrAcquireSwapchainImage (eye)");
		if (s.hasDepthExtension)
			AcquireSwapchainImage(s, s.depth, "xrAcquireSwapchainImage (depth)");
		AcquireSwapchainImage(s, s.layer, "xrAcquireSwapchainImage (layer)");
	}
	return true;
}


void GfxXRHandler::PrepareRelease(void)
noexcept
{
	if (not (m_state and m_state->frameBegun))
		return;

	XRState& s = *m_state;

	if (xrTestPattern and s.color.isAcquired and s.frameState.shouldRender)
		FillTestPattern(s);

	CommandList* cl = commandListHandler.CreateCmdList(String("xrRelease"), true);

	if (not (cl and cl->Open(false)))
		return;

	VkCommandBuffer cb = cl->GfxList();

	for (XRSwapchain* swapchain : { &s.color, &s.depth, &s.layer }) {
		if (swapchain->isAcquired)
			swapchain->targets[int32_t(swapchain->imageIndex)]->PrepareExternalRelease(cb);
	}
	cl->Close(false);
}


bool GfxXRHandler::EndFrame(void)
{
	if (not (m_state and m_state->frameBegun))
		return false;

	static uint32_t	loggedSubmit = 0xFFFFFFFFu;

	XRState&	s = *m_state;
	bool		hasColor = s.color.isAcquired and s.eyesRendered;
	bool		hasDepth = s.depth.isAcquired and s.depthSubmitted;
	bool		hasLayer = not xrTestPattern and s.layer.isAcquired and s.layerRendered and s.layerVisible;
	uint32_t	submit =(s.frameState.shouldRender ? 1u : 0u) | (s.color.isAcquired ? 2u : 0u) | (s.eyesRendered ? 4u : 0u) |
		(s.layer.isAcquired ? 8u : 0u) | (s.layerRendered ? 16u : 0u) | (s.layerVisible ? 32u : 0u) | (hasDepth ? 64u : 0u);

	if (submit != loggedSubmit) {
		logHandler.Print("OpenXR: frame - render %d, eye image acquired %d rendered %d, hud image acquired %d rendered %d visible %d, depth %d\n",
						 (submit & 1u) ? 1 : 0, (submit & 2u) ? 1 : 0, (submit & 4u) ? 1 : 0, (submit & 8u) ? 1 : 0, (submit & 16u) ? 1 : 0,
						 (submit & 32u) ? 1 : 0, (submit & 64u) ? 1 : 0);
		loggedSubmit = submit;
	}
	ReleaseSwapchainImage(s, s.color);
	ReleaseSwapchainImage(s, s.depth);
	ReleaseSwapchainImage(s, s.layer);

	XrCompositionLayerProjectionView	projectionViews[kMaxRenderViews];
	XrCompositionLayerDepthInfoKHR		depthInfos[kMaxRenderViews];
	XrCompositionLayerProjection		projection{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerQuad				quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader*	layers[2]{};
	uint32_t							layerCount = 0;

	if (s.frameState.shouldRender and hasColor) {
		for (int i = 0; i < kMaxRenderViews; ++i) {
			projectionViews[i] = XrCompositionLayerProjectionView{ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
			projectionViews[i].pose = s.views[i].pose;
			projectionViews[i].fov = s.views[i].fov;
			projectionViews[i].subImage.swapchain = s.color.handle;
			projectionViews[i].subImage.imageRect.offset = { 0, 0 };
			projectionViews[i].subImage.imageRect.extent = { s.color.width, s.color.height };
			projectionViews[i].subImage.imageArrayIndex = uint32_t(i);
			if (hasDepth) {
				depthInfos[i] = XrCompositionLayerDepthInfoKHR{ XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR };
				depthInfos[i].subImage.swapchain = s.depth.handle;
				depthInfos[i].subImage.imageRect.offset = { 0, 0 };
				depthInfos[i].subImage.imageRect.extent = { s.depth.width, s.depth.height };
				depthInfos[i].subImage.imageArrayIndex = uint32_t(i);
				depthInfos[i].minDepth = 0.0f;
				depthInfos[i].maxDepth = 1.0f;
				depthInfos[i].nearZ = s.zNear;
				depthInfos[i].farZ = s.zFar;
				projectionViews[i].next = &depthInfos[i];
			}
		}
		projection.space = s.appSpace;
		projection.viewCount = uint32_t(kMaxRenderViews);
		projection.views = projectionViews;
		layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
	}
	if (s.frameState.shouldRender and hasLayer) {
		quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		quad.space = s.appSpace;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = s.layer.handle;
		quad.subImage.imageRect.offset = { 0, 0 };
		quad.subImage.imageRect.extent = { s.layer.width, s.layer.height };
		quad.subImage.imageArrayIndex = 0;
		quad.pose = kIdentityPose;
		quad.pose.position.z = -s.layerDistance;
		quad.size.width = s.layerWidth;
		quad.size.height = s.layerWidth * float(s.layer.height) / float(s.layer.width);
		layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
	}

	XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };

	endInfo.displayTime = s.frameState.predictedDisplayTime;
	endInfo.environmentBlendMode = s.blendMode;
	endInfo.layerCount = layerCount;
	endInfo.layers = layers;
	s.frameBegun = false;
	return XrSucceeded(s.instance, xrEndFrame(s.session, &endInfo), "xrEndFrame");
}


void GfxXRHandler::Recenter(void)
noexcept
{
	if (HasSession())
		RecenterAppSpace(*m_state);
}


bool GfxXRHandler::IsAvailable(void) const
noexcept
{
	return m_state and (m_state->instance != XR_NULL_HANDLE) and (m_state->systemId != XR_NULL_SYSTEM_ID);
}


bool GfxXRHandler::HasSession(void) const
noexcept
{
	return IsAvailable() and (m_state->session != XR_NULL_HANDLE);
}


void GfxXRHandler::EnableSession(bool enable)
noexcept
{
	xrSessionEnabled = enable;
}


bool GfxXRHandler::SessionEnabled(void) const
noexcept
{
	return xrSessionEnabled;
}


void GfxXRHandler::SetTestPattern(bool enable)
noexcept
{
	xrTestPattern = enable;
	if (enable)
		logHandler.Print("OpenXR: test pattern on - eye images magenta, no HUD layer\n");
}


bool GfxXRHandler::DeviceCreated(void) const
noexcept
{
	return xrDeviceCreated;
}


bool GfxXRHandler::SessionStopped(void) const
noexcept
{
	return HasSession() and m_state->sessionStopped;
}


bool GfxXRHandler::SystemAvailable(void)
noexcept
{
	if (not IsAvailable())
		return false;
	if (m_state->exitRequested)
		return false;

	XrSystemGetInfo	systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
	XrSystemId		systemId = XR_NULL_SYSTEM_ID;

	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (XR_FAILED(xrGetSystem(m_state->instance, &systemInfo, &systemId)))
		return false;
	return systemId == m_state->systemId;
}


bool GfxXRHandler::IsRunning(void) const
noexcept
{
	return HasSession() and m_state->isRunning;
}


bool GfxXRHandler::IsFocused(void) const
noexcept
{
	return IsRunning() and (m_state->sessionState == XR_SESSION_STATE_FOCUSED);
}


bool GfxXRHandler::ShouldRender(void) const
noexcept
{
	return IsRunning() and m_state->frameBegun and (m_state->frameState.shouldRender == XR_TRUE);
}


bool GfxXRHandler::ExitRequested(void) const
noexcept
{
	return m_state and m_state->exitRequested;
}


int GfxXRHandler::EyeWidth(void) const
noexcept
{
	if (HasSession() and (m_state->color.width > 0))
		return m_state->color.width;
	if (not IsAvailable())
		return 0;

	int width = 0;
	int height = 0;

	RecommendedEyeSize(*m_state, width, height);
	return width;
}


int GfxXRHandler::EyeHeight(void) const
noexcept
{
	if (HasSession() and (m_state->color.height > 0))
		return m_state->color.height;
	if (not IsAvailable())
		return 0;

	int width = 0;
	int height = 0;

	RecommendedEyeSize(*m_state, width, height);
	return height;
}


Matrix4f GfxXRHandler::EyeProjection(int view, float zNear, float zFar) const
noexcept
{
	if (not (m_state and (view >= 0) and (view < kMaxRenderViews)))
		return Matrix4f::IDENTITY;

	const XrFovf& fov = m_state->views[view].fov;

	return Matrix4f(glm::frustum(std::tan(fov.angleLeft) * zNear, std::tan(fov.angleRight) * zNear, std::tan(fov.angleDown) * zNear,
								 std::tan(fov.angleUp) * zNear, zNear, zFar));
}


Matrix4f GfxXRHandler::EyeFromHead(int view) const
noexcept
{
	if (not (m_state and (view >= 0) and (view < kMaxRenderViews)))
		return Matrix4f::IDENTITY;
	return Matrix4f(glm::inverse(PoseMatrix(m_state->views[view].pose)) * PoseMatrix(m_state->headPose));
}


Matrix4f GfxXRHandler::HeadOrientation(void) const
noexcept
{
	if (not m_state)
		return Matrix4f::IDENTITY;

	const XrQuaternionf& q = m_state->headPose.orientation;

	return Matrix4f(glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z)));
}


Vector3f GfxXRHandler::HeadPosition(void) const
noexcept
{
	if (not m_state)
		return Vector3f{ 0.0f, 0.0f, 0.0f };

	const XrVector3f& p = m_state->headPose.position;

	return Vector3f{ p.x, p.y, p.z };
}


void GfxXRHandler::EyeTangents(int view, float& left, float& right, float& up, float& down) const
noexcept
{
	if (not (m_state and (view >= 0) and (view < kMaxRenderViews))) {
		left = -1.0f;
		right = 1.0f;
		up = 1.0f;
		down = -1.0f;
		return;
	}

	const XrFovf& fov = m_state->views[view].fov;

	left = std::tan(fov.angleLeft);
	right = std::tan(fov.angleRight);
	up = std::tan(fov.angleUp);
	down = std::tan(fov.angleDown);
}


RenderTarget* GfxXRHandler::EyeTarget(void)
noexcept
{
	return (m_state and m_state->color.isAcquired) ? m_state->color.targets[int32_t(m_state->color.imageIndex)] : nullptr;
}


RenderTarget* GfxXRHandler::DepthTarget(void)
noexcept
{
	return (m_state and m_state->depth.isAcquired) ? m_state->depth.targets[int32_t(m_state->depth.imageIndex)] : nullptr;
}


RenderTarget* GfxXRHandler::LayerTarget(void)
noexcept
{
	return (m_state and m_state->layer.isAcquired) ? m_state->layer.targets[int32_t(m_state->layer.imageIndex)] : nullptr;
}


void GfxXRHandler::SetEyesRendered(void)
noexcept
{
	if (m_state and m_state->color.isAcquired)
		m_state->eyesRendered = true;
}


bool GfxXRHandler::EyesRendered(void) const
noexcept
{
	return m_state and m_state->eyesRendered;
}


void GfxXRHandler::SetLayer(float distance, float width, bool visible)
noexcept
{
	if (not m_state)
		return;
	m_state->layerDistance = distance;
	m_state->layerWidth = width;
	m_state->layerVisible = visible;
}


bool GfxXRHandler::SubmitDepth(Texture* sceneDepth, float zNear, float zFar)
{
	RenderTarget* target = DepthTarget();

	if (not (target and sceneDepth))
		return false;

	Shader* shader = baseShaderHandler.SetupRenderShader("xrDepthCopy");

	if (not (shader and target->Activate({ .clear = false })))
		return false;
	baseRenderer.Set2DRenderStates();
	gfxStates.SetDepthTest(1);
	gfxStates.SetDepthWrite(1);
	gfxStates.DepthFunc(GfxOperations::CompareFunc::Always);
	baseRenderer.RenderToViewport(sceneDepth, ColorData::White, false, false, shader);
	target->Deactivate();
	baseRenderer.Set2DRenderStates();
	m_state->zNear = zNear;
	m_state->zFar = zFar;
	m_state->depthSubmitted = true;
	return true;
}


bool GfxXRHandler::DepthSubmitted(void) const
noexcept
{
	return m_state and m_state->depthSubmitted;
}


bool GfxXRHandler::RenderLayer(Texture* source, bool bRotate, bool bFlipVertically)
{
	RenderTarget* target = LayerTarget();

	if (not (target and source))
		return false;

	Shader* shader = baseShaderHandler.SetupRenderShader("xrLayerCopy");

	if (not (shader and target->Activate({ .clear = false })))
		return false;
	baseRenderer.Set2DRenderStates();
	baseRenderer.RenderToViewport(source, ColorData::White, bRotate, bFlipVertically, shader);
	target->Deactivate();
	m_state->layerRendered = true;
	return true;
}


static bool IsSRGBFormat(VkFormat format)
noexcept
{
	return (format == VK_FORMAT_B8G8R8A8_SRGB) or (format == VK_FORMAT_R8G8B8A8_SRGB) or (format == VK_FORMAT_A8B8G8R8_SRGB_PACK32);
}


void GfxXRHandler::WindowFit(int sourceWidth, int sourceHeight, int& x, int& y, int& width, int& height) const
noexcept
{
	int windowWidth = baseDisplayHandler.GetWidth();
	int windowHeight = baseDisplayHandler.GetHeight();

	if ((sourceWidth <= 0) or (sourceHeight <= 0) or (int64_t(windowWidth) * sourceHeight > int64_t(windowHeight) * sourceWidth)) {
		height = windowHeight;
		width = (sourceHeight > 0) ? int(int64_t(windowHeight) * sourceWidth / sourceHeight) : windowWidth;
	}
	else {
		width = windowWidth;
		height = int(int64_t(windowWidth) * sourceHeight / sourceWidth);
	}
	x = (windowWidth - width) / 2;
	y = (windowHeight - height) / 2;
}


bool GfxXRHandler::RenderMirror(bool bFlipVertically, int view)
{
	RenderTarget* eye = EyeTarget();

	if (not (eye and m_state->eyesRendered and (view >= 0) and (view < kMaxRenderViews)))
		return false;

	Texture* source = eye->GetAsTexture({});

	if (not source)
		return false;

	Shader* shader = baseShaderHandler.SetupRenderShader("xrMirror");

	if (not shader)
		return false;

	shader->SetInt("bEncodeSRGB", IsSRGBFormat(baseDisplayHandler.CurrentBackBufferFormat()) ? 0 : 1);
	shader->SetVector2f("tcOffset", Vector2f{ 0.0f, 0.0f });
	shader->SetVector2f("tcScale", Vector2f{ 1.0f, 1.0f });
	shader->SetFloat("eyeLayer", float(view));
	baseRenderer.Set2DRenderStates();
	baseRenderer.RenderToViewport(source, ColorData::White, false, bFlipVertically, shader);
	return true;
}


static constexpr float kMirrorZNear = 0.05f;
static constexpr float kMirrorZFar = 100.0f;

bool GfxXRHandler::RenderLayerMirror(Texture* source, bool bFlipVertically, int view)
{
	if (not (source and m_state and m_state->eyesRendered and m_state->layerVisible and (m_state->layerWidth > 0.0f) and
			 (m_state->layer.width > 0) and (view >= 0) and (view < kMaxRenderViews)))
		return false;

	Shader* shader = baseShaderHandler.SetupRenderShader("xrLayerMirror");

	if (not shader)
		return false;

	XRState&	s = *m_state;
	float		layerHeight = s.layerWidth * float(s.layer.height) / float(s.layer.width);
	glm::mat4	layerToApp = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -s.layerDistance)),
										glm::vec3(s.layerWidth, layerHeight, 1.0f));
	Matrix4f	layerToClip =
		EyeProjection(view, kMirrorZNear, kMirrorZFar) * Matrix4f(glm::inverse(PoseMatrix(s.views[view].pose)) * layerToApp);

	shader->SetMatrix4f("mLayerToClip", layerToClip.AsArray());
	shader->SetInt("bDecodeSRGB", IsSRGBFormat(baseDisplayHandler.CurrentBackBufferFormat()) ? 1 : 0);
	baseRenderer.RenderToViewport(source, ColorData::White, false, bFlipVertically, shader);
	return true;
}


bool GfxXRHandler::HasDepthLayer(void) const
noexcept
{
	return HasSession() and m_state->hasDepthExtension;
}


bool GfxXRHandler::HasVisibilityMask(void) const
noexcept
{
	return HasSession() and m_state->hasVisibilityMaskExtension;
}


bool GfxXRHandler::VisibilityMask(int view, const AutoArray<Vector2f>*& vertices, const AutoArray<uint32_t>*& indices) const
noexcept
{
	if (not (HasVisibilityMask() and (view >= 0) and (view < kMaxRenderViews)))
		return false;
	vertices = &m_state->maskVertices[view];
	indices = &m_state->maskIndices[view];
	return (vertices->Length() > 0) and (indices->Length() > 0);
}


uint32_t GfxXRHandler::VisibilityMaskVersion(void) const
noexcept
{
	return m_state ? m_state->maskVersion : 0;
}


const GfxXRHandler::ControllerState& GfxXRHandler::Controller(int hand) const
noexcept
{
	if (not (m_state and (hand >= 0) and (hand < xhCount)))
		return kNoController;
	return m_state->controllers[hand];
}


void GfxXRHandler::EnablePerformanceMetrics(bool enable)
noexcept
{
	if (not m_state)
		return;

	XRState& s = *m_state;

	s.performanceMetricsEnabled = enable;
	if (not (HasSession() and s.hasPerformanceMetricsExtension))
		return;

	XrPerformanceMetricsStateMETA state{ XR_TYPE_PERFORMANCE_METRICS_STATE_META };

	state.enabled = enable ? XR_TRUE : XR_FALSE;
	if (not XrSucceeded(s.instance, s.pfnSetPerformanceMetricsState(s.session, &state), "xrSetPerformanceMetricsStateMETA"))
		return;
	s.metricPaths.Reset();
	if (not enable)
		return;

	uint32_t pathCount = 0;

	if (XR_FAILED(s.pfnEnumeratePerformanceMetricsCounterPaths(s.instance, 0, &pathCount, nullptr)) or (pathCount == 0))
		return;
	s.metricPaths.Resize(int32_t(pathCount));
	if (XR_FAILED(s.pfnEnumeratePerformanceMetricsCounterPaths(s.instance, pathCount, &pathCount, s.metricPaths.Data())))
		s.metricPaths.Reset();
}


void GfxXRHandler::LogPerformanceMetrics(void)
noexcept
{
	if (not (HasSession() and m_state->performanceMetricsEnabled and (m_state->metricPaths.Length() > 0)))
		return;

	XRState& s = *m_state;

	for (int i = 0; i < s.metricPaths.Length(); ++i) {
		XrPerformanceMetricsCounterMETA	counter{ XR_TYPE_PERFORMANCE_METRICS_COUNTER_META };
		char							name[XR_MAX_PATH_LENGTH]{};
		uint32_t						nameLength = 0;

		if (XR_FAILED(s.pfnQueryPerformanceMetricsCounter(s.session, s.metricPaths[i], &counter)))
			continue;
		if (XR_FAILED(xrPathToString(s.instance, s.metricPaths[i], uint32_t(sizeof(name)), &nameLength, name)))
			continue;
		if (counter.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META)
			logHandler.Print("OpenXR metric %s: %.3f\n", name, counter.floatValue);
		else if (counter.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_UINT_VALUE_VALID_BIT_META)
			logHandler.Print("OpenXR metric %s: %u\n", name, counter.uintValue);
	}
}

// =================================================================================================
