
// =================================================================================================

#if defined(DIRECTX)
static GfxApiType gfxApiType = GfxApiType::DirectX;
#elif defined(VULKAN)
static GfxApiType gfxApiType = GfxApiType::Vulkan;
#else
static GfxApiType gfxApiType = GfxApiType::OpenGL;
#endif

bool IsGfxApi(GfxApiType value) noexcept {
	return value == gfxApiType;
}

// =================================================================================================
