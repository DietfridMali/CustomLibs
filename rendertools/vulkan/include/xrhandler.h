#pragma once

#include "vkframework.h"
#include "basesingleton.hpp"
#include "base_xrhandler.h"

// =================================================================================================

class GfxXRHandler
	: public BaseXRHandler,
	  public BaseSingleton<GfxXRHandler> {
public:
	GfxXRHandler();

	~GfxXRHandler();

	bool CreateVulkanInstance(const VkInstanceCreateInfo& info, VkInstance& instance)
	noexcept;

	bool GetVulkanPhysicalDevice(VkInstance instance, VkPhysicalDevice& physicalDevice)
	noexcept;

	bool CreateVulkanDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo& info, VkDevice& device)
	noexcept;

protected:
	virtual const char* GraphicsExtension(void) const
	noexcept override;

	virtual bool LoadGraphics(void) override;

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
