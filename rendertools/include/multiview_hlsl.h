#pragma once

#include "string.hpp"

inline const String& MultiviewHlslPrelude(int viewCount)
noexcept
{
	static const String stereo(R"(
#ifndef _MULTIVIEW_PRELUDE_
#define _MULTIVIEW_PRELUDE_
#define VIEW_COUNT 2
static uint gViewId = 0;
#define VIEW_ID_PARAM , uint viewIdInput : SV_ViewID
#define SET_VIEW_ID gViewId = viewIdInput;
#define ScreenTexture Texture2DArray
#define ScreenRWTexture RWTexture2DArray
#define SCREEN_UV(uv) float3 ((uv), float (gViewId))
#define SCREEN_LAYER(layer) ((layer) * VIEW_COUNT + gViewId)
#define SCREEN_TEXEL(xy) int4 ((xy), int (gViewId), 0)
#define SCREEN_STORE(xy, view) uint3 ((xy), (view))
#define SCREEN_DIMENSIONS(tex, w, h) { uint screenLayers; tex.GetDimensions (w, h, screenLayers); }
#define FRAME_EYE_MATRICES column_major float4x4 mFrameReserved; column_major float4x4 mEyeProjection [VIEW_COUNT]; column_major float4x4 mEyeView [VIEW_COUNT];
#define VIEW_PROJECTION mEyeProjection [gViewId]
#endif
)");
	static const String mono(R"(
#ifndef _MULTIVIEW_PRELUDE_
#define _MULTIVIEW_PRELUDE_
#define VIEW_COUNT 1
#define gViewId 0u
#define VIEW_ID_PARAM
#define SET_VIEW_ID
#define ScreenTexture Texture2D
#define ScreenRWTexture RWTexture2D
#define SCREEN_UV(uv) (uv)
#define SCREEN_LAYER(layer) (layer)
#define SCREEN_TEXEL(xy) int3 ((xy), 0)
#define SCREEN_STORE(xy, view) uint2 (xy)
#define SCREEN_DIMENSIONS(tex, w, h) { tex.GetDimensions (w, h); }
#define FRAME_EYE_MATRICES
#define VIEW_PROJECTION mProjection
#endif
)");
	return (viewCount > 1) ? stereo : mono;
}
