#pragma once

// =================================================================================================

enum class GfxApiType {
	OpenGL,
	DirectX,
	Vulkan,
	Unknown
};

inline static GfxApiType gfxApiType = GfxApiType::Unknown;

inline bool UsesGfxApi(GfxApiType value) noexcept {
	return value == gfxApiType;
}

inline bool UsesOpenGL(void) noexcept {
	return UsesGfxApi(GfxApiType::OpenGL);
}

inline bool UsesDirectX(void) noexcept {
	return UsesGfxApi(GfxApiType::DirectX);
}

inline bool UsesVulkan(void) noexcept {
	return UsesGfxApi(GfxApiType::Vulkan);
}

// =================================================================================================
