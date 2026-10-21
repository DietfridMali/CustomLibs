#pragma once

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "base_xrhandler.h"

// =================================================================================================

class GfxXRHandler
	: public BaseXRHandler,
	  public BaseSingleton<GfxXRHandler> {
public:
	GfxXRHandler();

	~GfxXRHandler();

	bool GetD3D12Adapter(IDXGIFactory4* factory, ComPtr<IDXGIAdapter1>& adapter, D3D_FEATURE_LEVEL& minFeatureLevel)
	noexcept;

	bool CreateD3D12Device(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL featureLevel, ComPtr<ID3D12Device>& device)
	noexcept;

protected:
	virtual const char* GraphicsExtension(void) const
	noexcept override;

	virtual bool LoadGraphics(String& info) override;

	virtual const void* GraphicsBinding(void)
	noexcept override;

	virtual void SwapchainFormats(const int64_t*& colorFormats, int& colorFormatCount, const int64_t*& depthFormats, int& depthFormatCount) const
	noexcept override;

	virtual bool CreateSwapchainTargets(XRSwapchain& swapchain, uint32_t imageCount, bool isDepth, const char* name) override;

	virtual bool BackBufferIsSRGB(void) const
	noexcept override;
};

#define xrHandler GfxXRHandler::Instance()

// =================================================================================================
