#define NOMINMAX

#include <cstdio>
#include <new>

#include "dx12framework.h"

#define XR_USE_GRAPHICS_API_D3D12
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "xrhandler.h"
#include "xrstate.h"
#include "dx12context.h"
#include "commandlist.h"
#include "rendertarget.h"
#include "base_displayhandler.h"
#include "loghandler.h"

// =================================================================================================

struct XRD3D12Requirements {
	PFN_xrGetD3D12GraphicsRequirementsKHR	pfnGetD3D12GraphicsRequirements{ nullptr };
	LUID									adapterLuid{};
	D3D_FEATURE_LEVEL						minFeatureLevel{ D3D_FEATURE_LEVEL_12_0 };
};

static XRD3D12Requirements xrD3D12;


static bool IsSRGBFormat(DXGI_FORMAT format)
noexcept
{
	return (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) or (format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) or
		(format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
}


static bool DeviceMeetsRequirements(void)
noexcept
{
	ID3D12Device* device = dx12Context.Device();

	if (not (device and dx12Context.HasMultiview()) or (dx12Context.FeatureLevel() < xrD3D12.minFeatureLevel))
		return false;

	LUID luid = device->GetAdapterLuid();

	return (luid.LowPart == xrD3D12.adapterLuid.LowPart) and (luid.HighPart == xrD3D12.adapterLuid.HighPart);
}

// =================================================================================================

GfxXRHandler::GfxXRHandler()
{
}


GfxXRHandler::~GfxXRHandler()
{
	Destroy();
}


const char* GfxXRHandler::GraphicsExtension(void) const
noexcept
{
	return XR_KHR_D3D12_ENABLE_EXTENSION_NAME;
}


bool GfxXRHandler::LoadGraphics(String& info)
{
	XRState& s = *m_state;

	if (not LoadXrFunction(s.instance, "xrGetD3D12GraphicsRequirementsKHR", xrD3D12.pfnGetD3D12GraphicsRequirements)) {
		logHandler.Print("OpenXR: the Direct3D 12 entry points of the runtime cannot be loaded\n");
		return false;
	}

	XrGraphicsRequirementsD3D12KHR requirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR };

	if (not XrSucceeded(s.instance, xrD3D12.pfnGetD3D12GraphicsRequirements(s.instance, s.systemId, &requirements),
						"xrGetD3D12GraphicsRequirementsKHR"))
		return false;
	xrD3D12.adapterLuid = requirements.adapterLuid;
	xrD3D12.minFeatureLevel = requirements.minFeatureLevel;
	if (DeviceMeetsRequirements())
		SetDeviceCreated();

	char text[64];

	snprintf(text, sizeof(text), "Direct3D 12 feature level %u.%u or higher", (unsigned(requirements.minFeatureLevel) >> 12) & 0xFu,
			 (unsigned(requirements.minFeatureLevel) >> 8) & 0xFu);
	info = String(text);
	return true;
}


const void* GfxXRHandler::GraphicsBinding(void)
noexcept
{
	static XrGraphicsBindingD3D12KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D12_KHR };

	binding.device = dx12Context.Device();
	binding.queue = commandListHandler.GetQueue();
	return &binding;
}


void GfxXRHandler::SwapchainFormats(const int64_t*& colorFormats, int& colorFormatCount, const int64_t*& depthFormats, int& depthFormatCount) const
noexcept
{
	static const int64_t colorFormatList[] = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R16G16B16A16_FLOAT,
											   DXGI_FORMAT_R10G10B10A2_UNORM,	DXGI_FORMAT_R8G8B8A8_UNORM,		 DXGI_FORMAT_B8G8R8A8_UNORM };
	static const int64_t depthFormatList[] = { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
											   DXGI_FORMAT_D16_UNORM };

	colorFormats = colorFormatList;
	colorFormatCount = int(sizeof(colorFormatList) / sizeof(colorFormatList[0]));
	depthFormats = depthFormatList;
	depthFormatCount = int(sizeof(depthFormatList) / sizeof(depthFormatList[0]));
}


bool GfxXRHandler::CreateSwapchainTargets(XRSwapchain& swapchain, uint32_t imageCount, bool isDepth, const char* name)
{
	AutoArray<XrSwapchainImageD3D12KHR> images;

	images.Resize(int32_t(imageCount));
	for (int i = 0; i < images.Length(); ++i)
		images[i] = XrSwapchainImageD3D12KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR };
	if (not XrSucceeded(m_state->instance,
						xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount,
												   reinterpret_cast<XrSwapchainImageBaseHeader*>(images.Data())),
						"xrEnumerateSwapchainImages"))
		return false;

	swapchain.targets.Resize(int32_t(imageCount));
	for (int i = 0; i < swapchain.targets.Length(); ++i)
		swapchain.targets[i] = nullptr;
	for (uint32_t i = 0; i < imageCount; ++i) {
		RenderTarget* target = new (std::nothrow) RenderTarget();

		if (not target)
			return false;
		swapchain.targets[int32_t(i)] = target;

		bool ok = isDepth ? target->CreateExternal(swapchain.width, swapchain.height, String(name), swapchain.layers, nullptr, DXGI_FORMAT_UNKNOWN,
												   images[int32_t(i)].texture, DXGI_FORMAT(swapchain.format))
						  : target->CreateExternal(swapchain.width, swapchain.height, String(name), swapchain.layers, images[int32_t(i)].texture,
												   DXGI_FORMAT(swapchain.format));

		if (not ok) {
			logHandler.Print("OpenXR: swapchain image %u of '%s' could not be wrapped\n", i, name);
			return false;
		}
	}
	return true;
}


bool GfxXRHandler::BackBufferIsSRGB(void) const
noexcept
{
	return IsSRGBFormat(BaseDisplayHandler::BACK_BUFFER_FORMAT);
}


bool GfxXRHandler::GetD3D12Adapter(IDXGIFactory4* factory, ComPtr<IDXGIAdapter1>& adapter, D3D_FEATURE_LEVEL& minFeatureLevel)
noexcept
{
	if (not (IsAvailable() and factory and xrD3D12.pfnGetD3D12GraphicsRequirements))
		return false;
	if (FAILED(factory->EnumAdapterByLuid(xrD3D12.adapterLuid, IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())))) {
		logHandler.Print("OpenXR: the graphics adapter of the headset was not found\n");
		return false;
	}
	minFeatureLevel = xrD3D12.minFeatureLevel;
	return true;
}


bool GfxXRHandler::CreateD3D12Device(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL featureLevel, ComPtr<ID3D12Device>& device)
noexcept
{
	if (not (IsAvailable() and adapter))
		return false;

	HRESULT hr = D3D12CreateDevice(adapter, featureLevel, IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));

	if (FAILED(hr)) {
		logHandler.Print("OpenXR: D3D12CreateDevice on the adapter of the headset failed (hr=0x%08X)\n", unsigned(hr));
		return false;
	}
	SetDeviceCreated();
	return true;
}

// =================================================================================================
