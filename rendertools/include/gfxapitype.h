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

bool HasOpenGL(void) noexcept;

bool HasDirectX(void) noexcept;

bool HasVulkan(void) noexcept;

// =================================================================================================
