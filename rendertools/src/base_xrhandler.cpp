#define NOMINMAX

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "base_xrhandler.h"
#include "xrstate.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

static const XrPosef kIdentityPose{ { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };

static bool xrSessionEnabled = true;
static bool xrDeviceCreated = false;
static std::atomic<bool> xrRuntimeMissing{ false };
static bool xrTestPattern = false;
#if LOG_FRAME_METRICS
static bool xrPerformanceMetrics = false;
#endif
static float xrViewPitch = 0.0f;
static int xrLayerWidth = 0;
static int xrLayerHeight = 0;

static const BaseXRHandler::ControllerState kNoController{};


bool XrSucceeded(XrInstance instance, XrResult result, const char* what)
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


static XrPosef MatrixPose(const glm::mat4& m)
noexcept
{
	glm::quat	orientation = glm::quat_cast(glm::mat3(m));
	XrPosef		pose;

	pose.orientation = { orientation.x, orientation.y, orientation.z, orientation.w };
	pose.position = { m[3].x, m[3].y, m[3].z };
	return pose;
}


static XrPosef PitchedPose(const XrPosef& pose, float degrees)
noexcept
{
	glm::quat	orientation(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
	glm::quat	pitched = orientation * glm::angleAxis(glm::radians(degrees), glm::vec3(1.0f, 0.0f, 0.0f));
	XrPosef		result = pose;

	result.orientation = { pitched.x, pitched.y, pitched.z, pitched.w };
	return result;
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


bool BaseXRHandler::CreateSwapchain(XRSwapchain& swapchain, int64_t format, uint64_t usage, int width, int height, int layers, bool isDepth,
									const char* name)
{
	XRState&				s = *m_state;
	XrSwapchainCreateInfo	info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };

	info.usageFlags = XrSwapchainUsageFlags(usage);
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
	return CreateSwapchainTargets(swapchain, imageCount, isDepth, name);
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
	s.appPose = pose;
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

	const XrQuaternionf&	head = location.pose.orientation;
	glm::vec3				forward = glm::quat(head.w, head.x, head.y, head.z) * glm::vec3(0.0f, 0.0f, -1.0f);
	float					yaw = std::atan2(-forward.x, -forward.z);
	glm::quat				level = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
	XrPosef					pose = kIdentityPose;

	pose.orientation = { level.x, level.y, level.z, level.w };
	if (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
		pose.position = location.pose.position;
	if (not CreateAppSpace(s, PitchedPose(pose, xrViewPitch)))
		return false;
	s.appliedViewPitch = xrViewPitch;
	logHandler.Print("OpenXR: reference pose set (head orientation %.3f %.3f %.3f %.3f, yaw %.1f, view pitch %+.0f)\n", head.x, head.y, head.z,
					 head.w, glm::degrees(yaw), xrViewPitch);
	return true;
}


static void DestroyProbeSession(XRState& s)
noexcept
{
	if (s.probeSession == XR_NULL_HANDLE)
		return;
	xrDestroySession(s.probeSession);
	s.probeSession = XR_NULL_HANDLE;
}


static bool ApplyLocalChange(XRState& s)
noexcept
{
	if (not CreateAppSpace(s, MatrixPose(glm::inverse(PoseMatrix(s.localChange)) * PoseMatrix(s.appPose))))
		return false;
	logHandler.Print("OpenXR: reference pose kept across the change of the local space\n");
	return true;
}


#if LOG_FRAME_METRICS

static constexpr float	kFrameLogTurn = 1.5f;
static constexpr int	kFrameLogFrames = 90;

static void LogFrame(XRState& s)
noexcept
{
	glm::quat	q(s.headPose.orientation.w, s.headPose.orientation.x, s.headPose.orientation.y, s.headPose.orientation.z);
	glm::vec3	f = q * glm::vec3(0.0f, 0.0f, -1.0f);
	float		yaw = glm::degrees(std::atan2(-f.x, -f.z));
	float		pitch = glm::degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f)));
	float		turn = std::fabs(yaw - s.lastYaw);

	if (turn > 180.0f)
		turn = 360.0f - turn;
	++s.frameNumber;
	if (turn > kFrameLogTurn)
		s.frameLogCount = kFrameLogFrames;
	if (s.frameLogCount > 0) {
		double appTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s.frameStart).count();

		--s.frameLogCount;
		logHandler.Print("OpenXR frame %u: display %+.2f ms (period %.2f ms), wait %.2f ms, app %.2f ms, render %d, eye image %u acquired %d rendered %d, head yaw %.2f pitch %.2f\n",
						 s.frameNumber, double(s.frameState.predictedDisplayTime - s.lastDisplayTime) * 1.0e-6,
						 double(s.frameState.predictedDisplayPeriod) * 1.0e-6, s.waitTime, appTime, s.frameState.shouldRender ? 1 : 0,
						 s.color.imageIndex, s.color.isAcquired ? 1 : 0, s.eyesRendered ? 1 : 0, yaw, pitch);
	}
	s.lastYaw = yaw;
	s.lastDisplayTime = s.frameState.predictedDisplayTime;
}

#endif


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
	info.countSubactionPaths = uint32_t(BaseXRHandler::xhCount);
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
	s.handPaths[BaseXRHandler::xhLeft] = StringToPath(s, "/user/hand/left");
	s.handPaths[BaseXRHandler::xhRight] = StringToPath(s, "/user/hand/right");
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
	for (int hand = 0; hand < BaseXRHandler::xhCount; ++hand)
		s.controllers[hand] = BaseXRHandler::ControllerState{};
	if ((s.actionSet == XR_NULL_HANDLE) or (s.sessionState != XR_SESSION_STATE_FOCUSED))
		return;

	XrActiveActionSet	activeSet{ s.actionSet, XR_NULL_PATH };
	XrActionsSyncInfo	syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };

	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeSet;
	if (XR_FAILED(xrSyncActions(s.session, &syncInfo)))
		return;
	for (int hand = 0; hand < BaseXRHandler::xhCount; ++hand) {
		BaseXRHandler::ControllerState&	controller = s.controllers[hand];
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
			controller.buttons |= BaseXRHandler::xbPrimary;
		if (BooleanAction(s, s.secondaryAction, s.handPaths[hand]))
			controller.buttons |= BaseXRHandler::xbSecondary;
		if (BooleanAction(s, s.menuAction, s.handPaths[hand]))
			controller.buttons |= BaseXRHandler::xbMenu;
		if (BooleanAction(s, s.thumbclickAction, s.handPaths[hand]))
			controller.buttons |= BaseXRHandler::xbThumbstick;
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

				if (changed.session != s.session)
					break;
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
					s.sessionStopped = true;
				}
				break;
			}

			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				logHandler.Print("OpenXR: instance loss pending\n");
				s.isRunning = false;
				s.instanceLost = true;
				s.sessionStopped = true;
				break;

			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				const XrEventDataReferenceSpaceChangePending& changed =
					*reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);
				const XrPosef& change = changed.poseInPreviousSpace;

				logHandler.Print("OpenXR: reference space %d change pending (pose valid %d, orientation %.3f %.3f %.3f %.3f, position %.3f %.3f %.3f)\n",
								 int(changed.referenceSpaceType), int(changed.poseValid), change.orientation.x, change.orientation.y,
								 change.orientation.z, change.orientation.w, change.position.x, change.position.y, change.position.z);
				if ((changed.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) and changed.poseValid) {
					s.localChange = s.localChangePending ? MatrixPose(PoseMatrix(s.localChange) * PoseMatrix(change)) : change;
					s.localChangeTime = changed.changeTime;
					s.localChangePending = true;
				}
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


static bool CreateXrInstance(XRState& s, const char* appName, const char* graphicsExtension)
{
	uint32_t	extensionCount = 0;
	XrResult	result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);

	xrRuntimeMissing = (result == XR_ERROR_RUNTIME_UNAVAILABLE);
	if (XR_FAILED(result)) {
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
	if (not HasExtension(extensions, graphicsExtension)) {
		logHandler.Print("OpenXR: the runtime has no %s\n", graphicsExtension);
		return false;
	}
	s.hasDepthExtension = HasExtension(extensions, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
	s.hasVisibilityMaskExtension = HasExtension(extensions, XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
#if LOG_FRAME_METRICS
	s.hasPerformanceMetricsExtension = HasExtension(extensions, XR_META_PERFORMANCE_METRICS_EXTENSION_NAME);
#endif
	s.hasCylinderExtension = HasExtension(extensions, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);

	const char*	enabledExtensions[5]{};
	uint32_t	enabledCount = 0;

	enabledExtensions[enabledCount++] = graphicsExtension;
	if (s.hasDepthExtension)
		enabledExtensions[enabledCount++] = XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME;
	if (s.hasVisibilityMaskExtension)
		enabledExtensions[enabledCount++] = XR_KHR_VISIBILITY_MASK_EXTENSION_NAME;
#if LOG_FRAME_METRICS
	if (s.hasPerformanceMetricsExtension)
		enabledExtensions[enabledCount++] = XR_META_PERFORMANCE_METRICS_EXTENSION_NAME;
#endif
	if (s.hasCylinderExtension)
		enabledExtensions[enabledCount++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;

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
	return true;
}


struct XRSystemQuery {
	std::thread			thread;
	std::atomic<bool>	isDone{ false };
	bool				isRunning{ false };
	bool				isCheck{ false };
	XRState*			pending{ nullptr };
	XrInstance			instance{ XR_NULL_HANDLE };
	String				appName;
	const char*			graphicsExtension{ nullptr };
	XrSystemId			systemId{ XR_NULL_SYSTEM_ID };
	XrResult			result{ XR_SUCCESS };

	~XRSystemQuery()
	{
		if (thread.joinable())
			thread.join();
	}
};

static XRSystemQuery xrSystemQuery;


static void RunSystemQuery(XRSystemQuery* query)
{
	if (not query->isCheck) {
		XRState& s = *query->pending;

		if ((s.instance == XR_NULL_HANDLE) and not CreateXrInstance(s, static_cast<const char*>(query->appName), query->graphicsExtension)) {
			query->result = XR_ERROR_INITIALIZATION_FAILED;
			query->isDone = true;
			return;
		}
		query->instance = s.instance;
	}

	XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };

	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	query->systemId = XR_NULL_SYSTEM_ID;
	query->result = xrGetSystem(query->instance, &systemInfo, &query->systemId);
	if (not query->isCheck) {
		XrSucceeded(query->instance, query->result, "xrGetSystem (no headset connected?)");
		if (query->result == XR_ERROR_INSTANCE_LOST) {
			xrDestroyInstance(query->instance);
			query->pending->instance = XR_NULL_HANDLE;
		}
	}
	query->isDone = true;
}


static void JoinSystemQuery(void)
noexcept
{
	if (xrSystemQuery.thread.joinable())
		xrSystemQuery.thread.join();
	xrSystemQuery.isRunning = false;
}

// =================================================================================================

bool BaseXRHandler::Create(const char* appName)
{
	if (IsAvailable())
		return true;
	JoinSystemQuery();
	if (not m_state) {
		m_state = xrSystemQuery.pending;
		xrSystemQuery.pending = nullptr;
	}
	if (not (m_state or (m_state = new (std::nothrow) XRState())))
		return false;

	XRState&	s = *m_state;

	if ((s.instance == XR_NULL_HANDLE) and not CreateXrInstance(s, appName, GraphicsExtension())) {
		xrSystemQuery.pending = m_state;
		m_state = nullptr;
		return false;
	}

	XrSystemGetInfo	systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
	XrSystemId		systemId = XR_NULL_SYSTEM_ID;

	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	XrResult result = xrGetSystem(s.instance, &systemInfo, &systemId);

	if (not XrSucceeded(s.instance, result, "xrGetSystem (no headset connected?)")) {
		if (result == XR_ERROR_INSTANCE_LOST)
			Destroy();
		else {
			xrSystemQuery.pending = m_state;
			m_state = nullptr;
		}
		return false;
	}
	s.systemId = systemId;
	return SetupSystem();
}


bool BaseXRHandler::StartSystemQuery(const char* appName)
{
	XRSystemQuery& q = xrSystemQuery;

	if (q.isRunning)
		return true;
	q.isCheck = IsAvailable();
	if (q.isCheck)
		q.instance = m_state->instance;
	else if (not (q.pending or (q.pending = new (std::nothrow) XRState())))
		return false;
	q.appName = String((appName and *appName) ? appName : "rendertools");
	q.graphicsExtension = GraphicsExtension();
	q.isDone = false;
	q.thread = std::thread(RunSystemQuery, &q);
	q.isRunning = true;
	return true;
}


int BaseXRHandler::SystemQueryResult(void)
{
	XRSystemQuery& q = xrSystemQuery;

	if (not q.isRunning)
		return xqNone;
	if (not q.isDone)
		return xqPending;
	q.thread.join();
	q.isRunning = false;
	if (q.isCheck) {
		if (q.result == XR_ERROR_INSTANCE_LOST)
			m_state->instanceLost = true;
		return (XR_SUCCEEDED(q.result) and (q.systemId == m_state->systemId)) ? xqAvailable : xqUnavailable;
	}
	if (XR_FAILED(q.result))
		return xqUnavailable;
	m_state = q.pending;
	q.pending = nullptr;
	m_state->systemId = q.systemId;
	return SetupSystem() ? xqAvailable : xqUnavailable;
}


bool BaseXRHandler::SetupSystem(void)
{
	XRState&	s = *m_state;
	uint32_t	viewCount = 0;

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

	if (s.hasVisibilityMaskExtension and not LoadXrFunction(s.instance, "xrGetVisibilityMaskKHR", s.pfnGetVisibilityMask))
		s.hasVisibilityMaskExtension = false;
#if LOG_FRAME_METRICS
	if (s.hasPerformanceMetricsExtension and
		not (LoadXrFunction(s.instance, "xrEnumeratePerformanceMetricsCounterPathsMETA", s.pfnEnumeratePerformanceMetricsCounterPaths) and
			 LoadXrFunction(s.instance, "xrSetPerformanceMetricsStateMETA", s.pfnSetPerformanceMetricsState) and
			 LoadXrFunction(s.instance, "xrQueryPerformanceMetricsCounterMETA", s.pfnQueryPerformanceMetricsCounter)))
		s.hasPerformanceMetricsExtension = false;
#endif

	String graphicsInfo;

	if (not LoadGraphics(graphicsInfo)) {
		Destroy();
		return false;
	}

	XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES };
	int eyeWidth = 0;
	int eyeHeight = 0;

	RecommendedEyeSize(s, eyeWidth, eyeHeight);
	if (XR_SUCCEEDED(xrGetSystemProperties(s.instance, s.systemId, &systemProperties)))
		logHandler.Print("OpenXR: %s, eye %d x %d, %s\n", systemProperties.systemName, eyeWidth, eyeHeight, static_cast<const char*>(graphicsInfo));
	return true;
}


void BaseXRHandler::Destroy(void)
noexcept
{
	JoinSystemQuery();
	if (xrSystemQuery.pending) {
		if (xrSystemQuery.pending->instance != XR_NULL_HANDLE)
			xrDestroyInstance(xrSystemQuery.pending->instance);
		delete xrSystemQuery.pending;
		xrSystemQuery.pending = nullptr;
	}
	if (not m_state)
		return;
	DestroySession();
	DestroyProbeSession(*m_state);
	if (m_state->instance != XR_NULL_HANDLE)
		xrDestroyInstance(m_state->instance);
	delete m_state;
	m_state = nullptr;
	xrDeviceCreated = false;
}


bool BaseXRHandler::CreateSession(int layerWidth, int layerHeight)
{
	if (not IsAvailable())
		return false;
	if (HasSession())
		return true;

	XRState&						s = *m_state;

	DestroyProbeSession(s);
	s.sessionStopped = false;
	s.recenterPending = true;
	s.localChangePending = false;
	XrSessionCreateInfo				sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };

	sessionInfo.next = GraphicsBinding();
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

	char	formatText[512];
	int		formatLength = snprintf(formatText, sizeof(formatText), "OpenXR: swapchain formats of the runtime:");

	for (int i = 0; (i < formats.Length()) and (formatLength < int(sizeof(formatText))); ++i)
		formatLength += snprintf(formatText + formatLength, sizeof(formatText) - size_t(formatLength), " %lld", static_cast<long long>(formats[i]));
	logHandler.Print("%s\n", formatText);

	const int64_t*	colorFormats = nullptr;
	const int64_t*	depthFormats = nullptr;
	int				colorFormatCount = 0;
	int				depthFormatCount = 0;

	SwapchainFormats(colorFormats, colorFormatCount, depthFormats, depthFormatCount);

	int64_t colorFormat = SelectFormat(formats, colorFormats, colorFormatCount, false);
	int64_t depthFormat = SelectFormat(formats, depthFormats, depthFormatCount, false);

	if (colorFormat == 0) {
		logHandler.Print("OpenXR: the runtime offers no usable colour swapchain format\n");
		DestroySession();
		return false;
	}

	int eyeWidth = 0;
	int eyeHeight = 0;

	RecommendedEyeSize(s, eyeWidth, eyeHeight);
	if (not CreateSwapchain(s.color, colorFormat,
							XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT, eyeWidth,
							eyeHeight, kMaxRenderViews, false, "xr eye buffer")) {
		DestroySession();
		return false;
	}
	if (s.hasDepthExtension and (depthFormat != 0) and
		not CreateSwapchain(s.depth, depthFormat, XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, eyeWidth, eyeHeight, kMaxRenderViews,
							true, "xr depth buffer")) {
		DestroySwapchain(s.depth);
		s.hasDepthExtension = false;
	}
	if ((layerWidth > 0) and (layerHeight > 0) and
		not CreateSwapchain(s.layer, colorFormat,
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
#if LOG_FRAME_METRICS
	if (xrPerformanceMetrics)
		EnablePerformanceMetrics(true);
#endif
	logHandler.Print("OpenXR: session created, eye buffers %d x %d (format %lld), depth layer %s, visibility mask %s, cylinder layer %s\n", eyeWidth,
					 eyeHeight, static_cast<long long>(colorFormat), s.hasDepthExtension ? "on" : "off", s.hasVisibilityMaskExtension ? "on" : "off",
					 s.hasCylinderExtension ? "on" : "off");
	return true;
}


void BaseXRHandler::DestroySession(void)
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


void BaseXRHandler::SetLayerSize(int width, int height)
{
	xrLayerWidth = width;
	xrLayerHeight = height;
	if (not HasSession())
		return;

	XRState& s = *m_state;

	if (((s.layer.width == LayerWidth()) and (s.layer.height == LayerHeight())) or s.layer.isAcquired or (s.color.format == 0))
		return;
	gfxStates.Finish();
	DestroySwapchain(s.layer);
	if (not CreateSwapchain(s.layer, s.color.format,
							XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT,
							LayerWidth(), LayerHeight(), 1, false, "xr layer buffer"))
		DestroySwapchain(s.layer);
	logHandler.Print("OpenXR: hud layer %d x %d\n", s.layer.width, s.layer.height);
}


int BaseXRHandler::LayerWidth(void) const
noexcept
{
	return ((xrLayerWidth > 0) and (xrLayerHeight > 0)) ? std::min(xrLayerWidth, EyeWidth()) : EyeWidth();
}


int BaseXRHandler::LayerHeight(void) const
noexcept
{
	return ((xrLayerWidth > 0) and (xrLayerHeight > 0)) ? int(std::lround(float(LayerWidth()) * float(xrLayerHeight) / float(xrLayerWidth))) : EyeHeight();
}


bool BaseXRHandler::BeginFrame(void)
{
	if (not HasSession())
		return false;

	XRState& s = *m_state;

	PollEvents(s);
	if (not s.isRunning)
		return false;

	XrFrameWaitInfo							waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
#if LOG_FRAME_METRICS
	std::chrono::steady_clock::time_point	waitStart = std::chrono::steady_clock::now();
#endif

	s.frameState = XrFrameState{ XR_TYPE_FRAME_STATE };
	if (not XrSucceeded(s.instance, xrWaitFrame(s.session, &waitInfo, &s.frameState), "xrWaitFrame"))
		return false;
#if LOG_FRAME_METRICS
	s.waitTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waitStart).count();
#endif

	XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };

	if (not XrSucceeded(s.instance, xrBeginFrame(s.session, &beginInfo), "xrBeginFrame"))
		return false;
	s.frameBegun = true;
	if (s.localChangePending and (s.frameState.predictedDisplayTime >= s.localChangeTime) and ApplyLocalChange(s))
		s.localChangePending = false;
	if (s.recenterPending and RecenterAppSpace(s))
		s.recenterPending = false;
	if ((s.appliedViewPitch != xrViewPitch) and (s.appSpace != XR_NULL_HANDLE) and
		CreateAppSpace(s, PitchedPose(s.appPose, xrViewPitch - s.appliedViewPitch)))
		s.appliedViewPitch = xrViewPitch;
	s.eyesRendered = false;
	s.layerRendered = false;
	s.layerInEyes = false;
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
#if LOG_FRAME_METRICS
	s.frameStart = std::chrono::steady_clock::now();
#endif
	return true;
}


void BaseXRHandler::PrepareRelease(void)
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
	for (XRSwapchain* swapchain : { &s.color, &s.depth, &s.layer }) {
		if (swapchain->isAcquired)
			swapchain->targets[int32_t(swapchain->imageIndex)]->PrepareExternalRelease(cl);
	}
	cl->Close(false);
}


bool BaseXRHandler::EndFrame(void)
{
	if (not (m_state and m_state->frameBegun))
		return false;

#if LOG_FRAME_METRICS
	static uint32_t	loggedSubmit = 0xFFFFFFFFu;
#endif

	XRState&	s = *m_state;
	bool		hasColor = s.color.isAcquired and s.eyesRendered;
	bool		hasDepth = s.depth.isAcquired and s.depthSubmitted;
	bool		hasLayer = not xrTestPattern and s.layer.isAcquired and s.layerRendered and s.layerVisible;
#if LOG_FRAME_METRICS
	uint32_t	submit =(s.frameState.shouldRender ? 1u : 0u) | (s.color.isAcquired ? 2u : 0u) | (s.eyesRendered ? 4u : 0u) |
		(s.layer.isAcquired ? 8u : 0u) | (s.layerRendered ? 16u : 0u) | (s.layerVisible ? 32u : 0u) | (hasDepth ? 64u : 0u);

	if (submit != loggedSubmit) {
		logHandler.Print("OpenXR: frame - render %d, eye image acquired %d rendered %d, hud image acquired %d rendered %d visible %d, depth %d\n",
						 (submit & 1u) ? 1 : 0, (submit & 2u) ? 1 : 0, (submit & 4u) ? 1 : 0, (submit & 8u) ? 1 : 0, (submit & 16u) ? 1 : 0,
						 (submit & 32u) ? 1 : 0, (submit & 64u) ? 1 : 0);
		loggedSubmit = submit;
	}
	LogFrame(s);
	if (s.performanceMetricsEnabled and (s.frameState.predictedDisplayTime - s.metricsTime >= 1000000000)) {
		s.metricsTime = s.frameState.predictedDisplayTime;
		LogPerformanceMetrics();
	}
#endif
	ReleaseSwapchainImage(s, s.color);
	ReleaseSwapchainImage(s, s.depth);
	ReleaseSwapchainImage(s, s.layer);

	XrCompositionLayerProjectionView	projectionViews[kMaxRenderViews];
	XrCompositionLayerDepthInfoKHR		depthInfos[kMaxRenderViews];
	XrCompositionLayerProjection		projection{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerQuad				quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerCylinderKHR		cylinder{ XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR };
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
	if (s.frameState.shouldRender and hasLayer and s.layerCurved) {
		cylinder.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		cylinder.space = s.appSpace;
		cylinder.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		cylinder.subImage.swapchain = s.layer.handle;
		cylinder.subImage.imageRect = s.layerRect;
		cylinder.subImage.imageArrayIndex = 0;
		cylinder.pose = kIdentityPose;
		cylinder.pose.position.z = s.layerRadius - s.layerDistance;
		cylinder.radius = s.layerRadius;
		cylinder.centralAngle = s.layerWidth / s.layerRadius;
		cylinder.aspectRatio = float(s.layerRect.extent.width) / float(s.layerRect.extent.height);
		layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cylinder);
	}
	else if (s.frameState.shouldRender and hasLayer) {
		quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		quad.space = s.appSpace;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = s.layer.handle;
		quad.subImage.imageRect = s.layerRect;
		quad.subImage.imageArrayIndex = 0;
		quad.pose = kIdentityPose;
		quad.pose.position.z = -s.layerDistance;
		quad.size.width = s.layerWidth;
		quad.size.height = s.layerWidth * float(s.layerRect.extent.height) / float(s.layerRect.extent.width);
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


void BaseXRHandler::Recenter(void)
noexcept
{
	if (HasSession()) {
		logHandler.Print("OpenXR: recenter requested\n");
		m_state->recenterPending = true;
	}
}


void BaseXRHandler::SetViewPitch(float degrees)
noexcept
{
	xrViewPitch = degrees;
}


bool BaseXRHandler::IsAvailable(void) const
noexcept
{
	return m_state and (m_state->instance != XR_NULL_HANDLE) and (m_state->systemId != XR_NULL_SYSTEM_ID);
}


bool BaseXRHandler::HasSession(void) const
noexcept
{
	return IsAvailable() and (m_state->session != XR_NULL_HANDLE);
}


void BaseXRHandler::EnableSession(bool enable)
noexcept
{
	xrSessionEnabled = enable;
}


bool BaseXRHandler::SessionEnabled(void) const
noexcept
{
	return xrSessionEnabled;
}


void BaseXRHandler::SetTestPattern(bool enable)
noexcept
{
	xrTestPattern = enable;
	if (enable)
		logHandler.Print("OpenXR: test pattern on - eye images magenta, no HUD layer\n");
}


void BaseXRHandler::SetDeviceCreated(void)
noexcept
{
	xrDeviceCreated = true;
}


bool BaseXRHandler::DeviceCreated(void) const
noexcept
{
	return xrDeviceCreated;
}


bool BaseXRHandler::SessionStopped(void) const
noexcept
{
	return HasSession() and m_state->sessionStopped;
}


bool BaseXRHandler::SystemAvailable(void)
noexcept
{
	if (not IsAvailable())
		return false;
	if (m_state->instanceLost)
		return false;

	XrSystemGetInfo	systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
	XrSystemId		systemId = XR_NULL_SYSTEM_ID;

	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	XrResult result = xrGetSystem(m_state->instance, &systemInfo, &systemId);

	if (result == XR_ERROR_INSTANCE_LOST)
		m_state->instanceLost = true;
	if (XR_FAILED(result))
		return false;
	return systemId == m_state->systemId;
}


bool BaseXRHandler::WaitUntilRunning(int timeout)
noexcept
{
	if (not HasSession())
		return false;

	XRState&								s = *m_state;
	std::chrono::steady_clock::time_point	end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

	for (;;) {
		PollEvents(s);
		if (s.isRunning)
			return true;
		if (s.instanceLost or s.sessionStopped or (std::chrono::steady_clock::now() >= end))
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}


bool BaseXRHandler::HeadsetReady(void)
noexcept
{
	if (not IsAvailable() or HasSession() or not xrDeviceCreated)
		return false;

	XRState& s = *m_state;

	if (s.probeSession == XR_NULL_HANDLE) {
		XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };

		sessionInfo.next = GraphicsBinding();
		sessionInfo.systemId = s.systemId;
		if (not XrSucceeded(s.instance, xrCreateSession(s.instance, &sessionInfo, &s.probeSession), "xrCreateSession (probe)")) {
			s.probeSession = XR_NULL_HANDLE;
			return false;
		}
		logHandler.Print("OpenXR: waiting for the headset\n");
	}

	XrEventDataBuffer	event{ XR_TYPE_EVENT_DATA_BUFFER };
	bool				ready = false;

	while (xrPollEvent(s.instance, &event) == XR_SUCCESS) {
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
			const XrEventDataSessionStateChanged& changed = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);

			if ((changed.session == s.probeSession) and (changed.state == XR_SESSION_STATE_READY))
				ready = true;
		}
		else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
			s.instanceLost = true;
		event = XrEventDataBuffer{ XR_TYPE_EVENT_DATA_BUFFER };
	}
	if (not ready)
		return false;
	DestroyProbeSession(s);
	logHandler.Print("OpenXR: headset ready again\n");
	return true;
}


bool BaseXRHandler::IsRunning(void) const
noexcept
{
	return HasSession() and m_state->isRunning;
}


bool BaseXRHandler::IsFocused(void) const
noexcept
{
	return IsRunning() and (m_state->sessionState == XR_SESSION_STATE_FOCUSED);
}


bool BaseXRHandler::ShouldRender(void) const
noexcept
{
	return IsRunning() and m_state->frameBegun and (m_state->frameState.shouldRender == XR_TRUE);
}


double BaseXRHandler::DisplayPeriod(void) const
noexcept
{
	return IsRunning() ? double(m_state->frameState.predictedDisplayPeriod) * 1.0e-6 : 0.0;
}


bool BaseXRHandler::InstanceLost(void) const
noexcept
{
	return m_state and m_state->instanceLost;
}


bool BaseXRHandler::RuntimeMissing(void) const
noexcept
{
	return xrRuntimeMissing;
}


int BaseXRHandler::EyeWidth(void) const
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


int BaseXRHandler::EyeHeight(void) const
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


Matrix4f BaseXRHandler::EyeProjection(int view, float zNear, float zFar) const
noexcept
{
	if (not (m_state and (view >= 0) and (view < kMaxRenderViews)))
		return Matrix4f::IDENTITY;

	const XrFovf& fov = m_state->views[view].fov;

	return Matrix4f(glm::frustum(std::tan(fov.angleLeft) * zNear, std::tan(fov.angleRight) * zNear, std::tan(fov.angleDown) * zNear,
								 std::tan(fov.angleUp) * zNear, zNear, zFar));
}


Matrix4f BaseXRHandler::EyeFromHead(int view) const
noexcept
{
	if (not (m_state and (view >= 0) and (view < kMaxRenderViews)))
		return Matrix4f::IDENTITY;
	return Matrix4f(glm::inverse(PoseMatrix(m_state->views[view].pose)) * PoseMatrix(m_state->headPose));
}


Matrix4f BaseXRHandler::HeadOrientation(void) const
noexcept
{
	if (not m_state)
		return Matrix4f::IDENTITY;

	const XrQuaternionf& q = m_state->headPose.orientation;

	return Matrix4f(glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z)));
}


Vector3f BaseXRHandler::HeadPosition(void) const
noexcept
{
	if (not m_state)
		return Vector3f{ 0.0f, 0.0f, 0.0f };

	const XrVector3f& p = m_state->headPose.position;

	return Vector3f{ p.x, p.y, p.z };
}


void BaseXRHandler::EyeTangents(int view, float& left, float& right, float& up, float& down) const
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


RenderTarget* BaseXRHandler::EyeTarget(void)
noexcept
{
	return (m_state and m_state->color.isAcquired) ? m_state->color.targets[int32_t(m_state->color.imageIndex)] : nullptr;
}


RenderTarget* BaseXRHandler::DepthTarget(void)
noexcept
{
	return (m_state and m_state->depth.isAcquired) ? m_state->depth.targets[int32_t(m_state->depth.imageIndex)] : nullptr;
}


RenderTarget* BaseXRHandler::LayerTarget(void)
noexcept
{
	return (m_state and m_state->layer.isAcquired) ? m_state->layer.targets[int32_t(m_state->layer.imageIndex)] : nullptr;
}


void BaseXRHandler::SetEyesRendered(void)
noexcept
{
	if (m_state and m_state->color.isAcquired)
		m_state->eyesRendered = true;
}


bool BaseXRHandler::EyesRendered(void) const
noexcept
{
	return m_state and m_state->eyesRendered;
}


void BaseXRHandler::SetLayer(float distance, float width, bool visible, bool curved, float radius)
noexcept
{
	if (not m_state)
		return;
	m_state->layerDistance = distance;
	m_state->layerWidth = width;
	m_state->layerVisible = visible;
	m_state->layerCurved = curved and m_state->hasCylinderExtension;
	m_state->layerRadius = radius;
}


bool BaseXRHandler::HasCurvedLayer(void) const
noexcept
{
	return HasSession() and m_state->hasCylinderExtension;
}


void BaseXRHandler::SetLayerFovScale(float scale)
noexcept
{
	if (not m_state)
		return;
	if ((scale > 1.0f) != (m_state->layerFovScale > 1.0f)) {
		if (scale > 1.0f)
			logHandler.Print("OpenXR: hud rendered into the eye images (field of view scale %.2f)\n", scale);
		else
			logHandler.Print("OpenXR: hud submitted as composition layer\n");
	}
	m_state->layerFovScale = scale;
}


bool BaseXRHandler::SubmitDepth(Texture* sceneDepth, float zNear, float zFar)
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


bool BaseXRHandler::DepthSubmitted(void) const
noexcept
{
	return m_state and m_state->depthSubmitted;
}


void BaseXRHandler::SetLayerRect(int width, int height)
noexcept
{
	XRState& s = *m_state;

	if ((width <= 0) or (height <= 0)) {
		width = s.layer.width;
		height = s.layer.height;
	}
	s.layerRect.offset = { (s.layer.width - width) / 2, (s.layer.height - height) / 2 };
	s.layerRect.extent = { width, height };
}


bool BaseXRHandler::RenderLayer(Texture* source, bool bRotate, bool bFlipVertically, int width, int height)
{
	if (m_state)
		SetLayerRect(width, height);
	if (source and m_state and (m_state->layerFovScale > 1.0f) and m_state->layerVisible and m_state->eyesRendered)
		return RenderLayerToEyes(source, bFlipVertically);

	RenderTarget* target = LayerTarget();

	if (not (target and source))
		return false;

	Shader* shader = baseShaderHandler.SetupRenderShader("xrLayerCopy");

	if (not (shader and target->Activate({ .clear = false })))
		return false;
	baseRenderer.Set2DRenderStates();
	baseRenderer.SetViewport(::Viewport(m_state->layerRect.offset.x, m_state->layerRect.offset.y, m_state->layerRect.extent.width, m_state->layerRect.extent.height));
	baseRenderer.RenderToViewport(source, ColorData::White, bRotate, bFlipVertically, shader);
	target->Deactivate();
	m_state->layerRendered = true;
	return true;
}


void BaseXRHandler::WindowFit(int sourceWidth, int sourceHeight, int& x, int& y, int& width, int& height) const
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


bool BaseXRHandler::RenderMirror(bool bFlipVertically, int view)
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

	shader->SetInt("bEncodeSRGB", BackBufferIsSRGB() ? 0 : 1);
	shader->SetVector2f("tcOffset", Vector2f{ 0.0f, 0.0f });
	shader->SetVector2f("tcScale", Vector2f{ 1.0f, 1.0f });
	shader->SetFloat("eyeLayer", float(view));
	baseRenderer.Set2DRenderStates();
	baseRenderer.RenderToViewport(source, ColorData::White, false, bFlipVertically, shader);
	return true;
}


static constexpr float	kLayerZNearScale = 0.1f;
static constexpr float	kLayerZFarScale = 10.0f;
static constexpr int	kMirrorStrips = 32;

bool BaseXRHandler::RenderLayerMirror(Texture* source, bool bFlipVertically, int view)
{
	if (m_state and m_state->layerInEyes)
		return true;
	if (not (source and m_state and m_state->eyesRendered and m_state->layerVisible and (m_state->layerWidth > 0.0f) and
			 (m_state->layer.width > 0) and (view >= 0) and (view < kMaxRenderViews)))
		return false;

	Shader* shader = baseShaderHandler.SetupRenderShader("xrLayerMirror");

	if (not shader)
		return false;

	XRState&	s = *m_state;
	float		layerHeight = s.layerWidth * float(s.layerRect.extent.height) / float(s.layerRect.extent.width);
	float		zNear = s.layerDistance * kLayerZNearScale;
	float		zFar = s.layerDistance * kLayerZFarScale;

	shader->SetInt("bDecodeSRGB", BackBufferIsSRGB() ? 1 : 0);
	if (not s.layerCurved) {
		glm::mat4	layerToApp = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -s.layerDistance)),
											glm::vec3(s.layerWidth, layerHeight, 1.0f));
		Matrix4f	layerToClip = EyeProjection(view, zNear, zFar) * Matrix4f(glm::inverse(PoseMatrix(s.views[view].pose)) * layerToApp);

		shader->SetMatrix4f("mLayerToClip", layerToClip.AsArray());
		shader->SetFloat("fCentralAngle", 0.0f);
		baseRenderer.RenderToViewport(source, ColorData::White, false, bFlipVertically, shader);
		return true;
	}

	Matrix4f layerToClip = EyeProjection(view, zNear, zFar) * Matrix4f(glm::inverse(PoseMatrix(s.views[view].pose)));

	shader->SetMatrix4f("mLayerToClip", layerToClip.AsArray());
	shader->SetFloat("fCentralAngle", s.layerWidth / s.layerRadius);
	shader->SetFloat("fRadius", s.layerRadius);
	shader->SetFloat("fCenterDistance", s.layerDistance - s.layerRadius);
	shader->SetFloat("fLayerHeight", layerHeight);
	shader->SetFloat("fStripWidth", 1.0f / float(kMirrorStrips));
	for (int i = 0; i < kMirrorStrips; ++i) {
		shader->SetFloat("fStripStart", float(i) / float(kMirrorStrips));
		baseRenderer.RenderToViewport(source, ColorData::White, false, bFlipVertically, shader);
	}
	return true;
}


bool BaseXRHandler::RenderLayerToEyes(Texture* source, bool bFlipVertically)
{
	XRState& s = *m_state;

	if (not (s.layer.width > 0))
		return false;
	if (not RenderPlaneToEyes(source, s.layerDistance, s.layerWidth, s.layerWidth * float(s.layerRect.extent.height) / float(s.layerRect.extent.width),
							  s.layerCurved, bFlipVertically))
		return false;
	s.layerInEyes = true;
	return true;
}


bool BaseXRHandler::RenderImageToEyes(Texture* source, float widthScale, float aspect, bool bFlipVertically)
{
	if (not (source and m_state and m_state->eyesRendered and (aspect > 0.0f)))
		return false;

	float width = m_state->layerWidth * widthScale;

	return RenderPlaneToEyes(source, m_state->layerDistance, width, width / aspect, false, bFlipVertically);
}


bool BaseXRHandler::RenderImageToEyesAt(Texture* source, float distance, float widthScale, float aspect, bool bFlipVertically)
{
	if (not (source and m_state and m_state->eyesRendered and (distance > 0.0f) and (aspect > 0.0f)))
		return false;

	float width = m_state->layerWidth * widthScale * distance / m_state->layerDistance;

	return RenderPlaneToEyes(source, distance, width, width / aspect, false, bFlipVertically);
}


bool BaseXRHandler::RenderImageToEyes(Texture* source, const Matrix4f* imageToClip, bool bFlipVertically)
{
	if (not (source and imageToClip and m_state and m_state->eyesRendered))
		return false;
	return DrawToEyes(source, imageToClip, false, 0.0f, 0.0f, 0.0f, bFlipVertically);
}


bool BaseXRHandler::EyeImageCenter(int view, float distance, Vector2f& ndc) const
noexcept
{
	if (not (m_state and (distance > 0.0f) and (view >= 0) and (view < kMaxRenderViews)))
		return false;

	Vector4f clip = AppToClip(view, distance * kLayerZNearScale, distance * kLayerZFarScale) * Vector4f(0.0f, 0.0f, -distance, 1.0f);

	if (clip.w <= 0.0f)
		return false;
	ndc = Vector2f(clip.x / clip.w, clip.y / clip.w);
	return true;
}


Matrix4f BaseXRHandler::AppToClip(int view, float zNear, float zFar) const
noexcept
{
	const XRState&	s = *m_state;
	const XrFovf&	fov = s.views[view].fov;
	float			tanScale = s.layerFovScale * zNear;
	glm::mat4		stretch = glm::scale(glm::mat4(1.0f), glm::vec3(s.layerFovScale, s.layerFovScale, 1.0f));

	return Matrix4f(glm::frustum(std::tan(fov.angleLeft) * tanScale, std::tan(fov.angleRight) * tanScale,
								 std::tan(fov.angleDown) * tanScale, std::tan(fov.angleUp) * tanScale, zNear, zFar) *
					glm::inverse(PoseMatrix(s.views[view].pose)) * stretch);
}


bool BaseXRHandler::RenderPlaneToEyes(Texture* source, float distance, float width, float height, bool curved, bool bFlipVertically)
{
	if (not (width > 0.0f))
		return false;

	float		zNear = distance * kLayerZNearScale;
	float		zFar = distance * kLayerZFarScale;
	glm::mat4	layerToApp = curved ? glm::mat4(1.0f)
									: glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -distance)), glm::vec3(width, height, 1.0f));
	Matrix4f	layerToClip[kMaxRenderViews];

	for (int i = 0; i < kMaxRenderViews; ++i)
		layerToClip[i] = AppToClip(i, zNear, zFar) * layerToApp;
	return DrawToEyes(source, layerToClip, curved, distance, width, height, bFlipVertically);
}


bool BaseXRHandler::DrawToEyes(Texture* source, const Matrix4f* layerToClip, bool curved, float distance, float width, float height, bool bFlipVertically)
{
	XRState&		s = *m_state;
	RenderTarget*	eyes = EyeTarget();

	if (not eyes)
		return false;

	Shader* shader = baseShaderHandler.SetupRenderShader("xrLayerEyes");

	if (not (shader and eyes->Activate({ .clear = false })))
		return false;
	baseRenderer.Set2DRenderStates();
	gfxStates.SetBlending(1);
	gfxStates.BlendFunc(GfxOperations::BlendFactor::One, GfxOperations::BlendFactor::InvSrcAlpha);
	shader->SetMatrix4fArray("mLayerToClip", layerToClip[0].AsArray(), baseRenderer.ViewCount());
	if (not curved) {
		shader->SetFloat("fCentralAngle", 0.0f);
		baseRenderer.RenderToViewport(source, ColorData::White, false, bFlipVertically, shader);
	}
	else {
		shader->SetFloat("fCentralAngle", width / s.layerRadius);
		shader->SetFloat("fRadius", s.layerRadius);
		shader->SetFloat("fCenterDistance", distance - s.layerRadius);
		shader->SetFloat("fLayerHeight", height);
		shader->SetFloat("fStripWidth", 1.0f / float(kMirrorStrips));
		for (int i = 0; i < kMirrorStrips; ++i) {
			shader->SetFloat("fStripStart", float(i) / float(kMirrorStrips));
			baseRenderer.RenderToViewport(source, ColorData::White, false, bFlipVertically, shader);
		}
	}
	eyes->Deactivate();
	baseRenderer.Set2DRenderStates();
	return true;
}


bool BaseXRHandler::HasDepthLayer(void) const
noexcept
{
	return HasSession() and m_state->hasDepthExtension;
}


bool BaseXRHandler::HasVisibilityMask(void) const
noexcept
{
	return HasSession() and m_state->hasVisibilityMaskExtension;
}


bool BaseXRHandler::VisibilityMask(int view, const AutoArray<Vector2f>*& vertices, const AutoArray<uint32_t>*& indices) const
noexcept
{
	if (not (HasVisibilityMask() and (view >= 0) and (view < kMaxRenderViews)))
		return false;
	vertices = &m_state->maskVertices[view];
	indices = &m_state->maskIndices[view];
	return (vertices->Length() > 0) and (indices->Length() > 0);
}


uint32_t BaseXRHandler::VisibilityMaskVersion(void) const
noexcept
{
	return m_state ? m_state->maskVersion : 0;
}


const BaseXRHandler::ControllerState& BaseXRHandler::Controller(int hand) const
noexcept
{
	if (not (m_state and (hand >= 0) and (hand < xhCount)))
		return kNoController;
	return m_state->controllers[hand];
}

#if LOG_FRAME_METRICS

void BaseXRHandler::EnablePerformanceMetrics(bool enable)
noexcept
{
	xrPerformanceMetrics = enable;
	if (not m_state)
		return;

	XRState& s = *m_state;

	s.performanceMetricsEnabled = enable;
	if (not HasSession())
		return;
	if (not s.hasPerformanceMetricsExtension) {
		if (enable)
			logHandler.Print("OpenXR: the runtime offers no performance metrics\n");
		return;
	}

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
	logHandler.Print("OpenXR: performance metrics on, %d counters\n", s.metricPaths.Length());
}


void BaseXRHandler::LogPerformanceMetrics(void)
noexcept
{
	if (not (HasSession() and m_state->performanceMetricsEnabled and (m_state->metricPaths.Length() > 0)))
		return;

	static const char	prefix[] = "/perfmetrics_meta/";

	XRState&	s = *m_state;
	char		line[2048];
	int			length = snprintf(line, sizeof(line), "OpenXR metrics:");

	for (int i = 0; (i < s.metricPaths.Length()) and (length < int(sizeof(line)) - 1); ++i) {
		XrPerformanceMetricsCounterMETA	counter{ XR_TYPE_PERFORMANCE_METRICS_COUNTER_META };
		char							name[XR_MAX_PATH_LENGTH]{};
		uint32_t						nameLength = 0;

		if (XR_FAILED(s.pfnQueryPerformanceMetricsCounter(s.session, s.metricPaths[i], &counter)))
			continue;
		if (XR_FAILED(xrPathToString(s.instance, s.metricPaths[i], uint32_t(sizeof(name)), &nameLength, name)))
			continue;

		const char* shortName = (std::strncmp(name, prefix, sizeof(prefix) - 1) == 0) ? name + sizeof(prefix) - 1 : name;

		if (counter.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META)
			length += snprintf(line + length, sizeof(line) - size_t(length), " %s %.2f", shortName, counter.floatValue);
		else if (counter.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_UINT_VALUE_VALID_BIT_META)
			length += snprintf(line + length, sizeof(line) - size_t(length), " %s %u", shortName, counter.uintValue);
	}
	logHandler.Print("%s\n", line);
}

#endif

// =================================================================================================
