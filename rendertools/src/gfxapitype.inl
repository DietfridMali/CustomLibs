
// =================================================================================================

BaseRenderer::GfxApiType BaseRenderer::gfxApiType =
#if defined(DIRECTX)
	BaseRenderer::GfxApiType::DirectX;
#elif defined(VULKAN)
	BaseRenderer::GfxApiType::Vulkan;
#else
	BaseRenderer::GfxApiType::OpenGL;
#endif

bool BaseRenderer::IsGfxApi(GfxApiType value) noexcept {
	return value == gfxApiType;
}

bool BaseRenderer::HasOpenGL(void) noexcept {
	return IsGfxApi(GfxApiType::OpenGL);
}

bool BaseRenderer::HasDirectX(void) noexcept {
	return IsGfxApi(GfxApiType::DirectX);
}

bool BaseRenderer::HasVulkan(void) noexcept {
	return IsGfxApi(GfxApiType::Vulkan);
}

// =================================================================================================
