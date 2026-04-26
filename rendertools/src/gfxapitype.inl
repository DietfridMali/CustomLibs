
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

// =================================================================================================
