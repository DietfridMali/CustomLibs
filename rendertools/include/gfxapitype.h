#pragma once

// =================================================================================================

enum class GfxApiType {
	OpenGL,
	DirectX,
	Vulkan,
	Unknown
};

static GfxApiType gfxApiType;

bool IsGfxApi(GfxApiType value) noexcept;

// =================================================================================================
