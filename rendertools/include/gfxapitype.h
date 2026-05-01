#pragma once

// =================================================================================================

enum class GfxApiType {
	OpenGL,
	DirectX,
	Vulkan,
	Unknown
};

inline static GfxApiType gfxApiType = GfxApiType::Unknown;

inline bool IsGfxApi(GfxApiType value) noexcept {
	return value == gfxApiType;
}

inline bool HasOpenGL(void) noexcept {
	return IsGfxApi(GfxApiType::OpenGL);
}

inline bool HasDirectX(void) noexcept {
	return IsGfxApi(GfxApiType::DirectX);
}

inline bool HasVulkan(void) noexcept {
	return IsGfxApi(GfxApiType::Vulkan);
}

// =================================================================================================
