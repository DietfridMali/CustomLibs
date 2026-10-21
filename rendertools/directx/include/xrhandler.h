#pragma once

#include <cstdint>

#include "basesingleton.hpp"
#include "array.hpp"
#include "vector.hpp"
#include "matrix.hpp"
#include "rendertypes.h"

class RenderTarget;
class Texture;

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

	inline bool Create(const char*)
	{
		return false;
	}

	inline void Destroy(void)
	noexcept
	{}

	inline bool CreateSession(int, int)
	{
		return false;
	}

	inline void DestroySession(void)
	noexcept
	{}

	inline bool BeginFrame(void)
	{
		return false;
	}

	inline bool EndFrame(void)
	{
		return false;
	}

	inline void PrepareRelease(void)
	noexcept
	{}

	inline void Recenter(void)
	noexcept
	{}

	inline bool IsAvailable(void) const
	noexcept
	{
		return false;
	}

	inline bool HasSession(void) const
	noexcept
	{
		return false;
	}

	inline void EnableSession(bool)
	noexcept
	{}

	inline bool SessionEnabled(void) const
	noexcept
	{
		return false;
	}

	inline bool DeviceCreated(void) const
	noexcept
	{
		return false;
	}

	inline bool SessionStopped(void) const
	noexcept
	{
		return false;
	}

	inline bool SystemAvailable(void)
	noexcept
	{
		return false;
	}

	inline bool HeadsetReady(void)
	noexcept
	{
		return false;
	}

	inline bool WaitUntilRunning(int timeout)
	noexcept
	{
		return false;
	}

	inline bool IsRunning(void) const
	noexcept
	{
		return false;
	}

	inline bool IsFocused(void) const
	noexcept
	{
		return false;
	}

	inline bool ShouldRender(void) const
	noexcept
	{
		return false;
	}

	inline bool ExitRequested(void) const
	noexcept
	{
		return false;
	}

	inline int EyeWidth(void) const
	noexcept
	{
		return 0;
	}

	inline int EyeHeight(void) const
	noexcept
	{
		return 0;
	}

	inline Matrix4f EyeProjection(int, float, float) const
	noexcept
	{
		return Matrix4f::IDENTITY;
	}

	inline Matrix4f EyeFromHead(int) const
	noexcept
	{
		return Matrix4f::IDENTITY;
	}

	inline Matrix4f HeadOrientation(void) const
	noexcept
	{
		return Matrix4f::IDENTITY;
	}

	inline Vector3f HeadPosition(void) const
	noexcept
	{
		return Vector3f{ 0.0f, 0.0f, 0.0f };
	}

	inline void EyeTangents(int, float& left, float& right, float& up, float& down) const
	noexcept
	{
		left = -1.0f;
		right = 1.0f;
		up = 1.0f;
		down = -1.0f;
	}

	inline RenderTarget* EyeTarget(void)
	noexcept
	{
		return nullptr;
	}

	inline RenderTarget* DepthTarget(void)
	noexcept
	{
		return nullptr;
	}

	inline RenderTarget* LayerTarget(void)
	noexcept
	{
		return nullptr;
	}

	inline void SetEyesRendered(void)
	noexcept
	{}

	inline bool EyesRendered(void) const
	noexcept
	{
		return false;
	}

	inline void SetLayer(float, float, bool)
	noexcept
	{}

	inline bool SubmitDepth(Texture*, float, float)
	{
		return false;
	}

	inline bool DepthSubmitted(void) const
	noexcept
	{
		return false;
	}

	inline bool RenderLayer(Texture*, bool, bool)
	{
		return false;
	}

	inline void WindowFit(int sourceWidth, int sourceHeight, int& x, int& y, int& width, int& height) const
	noexcept
	{
		x = 0;
		y = 0;
		width = sourceWidth;
		height = sourceHeight;
	}

	inline bool RenderMirror(bool, int)
	{
		return false;
	}

	inline bool RenderLayerMirror(Texture*, bool, int)
	{
		return false;
	}

	inline void SetTestPattern(bool)
	noexcept
	{}

	inline bool HasDepthLayer(void) const
	noexcept
	{
		return false;
	}

	inline bool HasVisibilityMask(void) const
	noexcept
	{
		return false;
	}

	inline bool VisibilityMask(int, const AutoArray<Vector2f>*&, const AutoArray<uint32_t>*&) const
	noexcept
	{
		return false;
	}

	inline uint32_t VisibilityMaskVersion(void) const
	noexcept
	{
		return 0;
	}

	inline const ControllerState& Controller(int) const
	noexcept
	{
		static const ControllerState noController{};
		return noController;
	}

	inline void EnablePerformanceMetrics(bool)
	noexcept
	{}

	inline void LogPerformanceMetrics(void)
	noexcept
	{}
};

#define xrHandler GfxXRHandler::Instance()

// =================================================================================================
