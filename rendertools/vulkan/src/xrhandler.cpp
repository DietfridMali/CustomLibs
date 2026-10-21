#define NOMINMAX

#include <cstdio>
#include <new>

#include "vkframework.h"

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "xrhandler.h"
#include "xrstate.h"
#include "vkcontext.h"
#include "rendertarget.h"
#include "base_displayhandler.h"
#include "loghandler.h"

// =================================================================================================

struct XRVulkanFunctions {
	PFN_xrGetVulkanGraphicsRequirements2KHR	pfnGetVulkanGraphicsRequirements2{ nullptr };
	PFN_xrCreateVulkanInstanceKHR			pfnCreateVulkanInstance{ nullptr };
	PFN_xrCreateVulkanDeviceKHR				pfnCreateVulkanDevice{ nullptr };
	PFN_xrGetVulkanGraphicsDevice2KHR		pfnGetVulkanGraphicsDevice2{ nullptr };
};

static XRVulkanFunctions xrVulkan;


static bool IsSRGBFormat(VkFormat format)
noexcept
{
	return (format == VK_FORMAT_B8G8R8A8_SRGB) or (format == VK_FORMAT_R8G8B8A8_SRGB) or (format == VK_FORMAT_A8B8G8R8_SRGB_PACK32);
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
	return XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
}


bool GfxXRHandler::LoadGraphics(String& info)
{
	XRState& s = *m_state;

	if (not (LoadXrFunction(s.instance, "xrGetVulkanGraphicsRequirements2KHR", xrVulkan.pfnGetVulkanGraphicsRequirements2) and
			 LoadXrFunction(s.instance, "xrCreateVulkanInstanceKHR", xrVulkan.pfnCreateVulkanInstance) and
			 LoadXrFunction(s.instance, "xrCreateVulkanDeviceKHR", xrVulkan.pfnCreateVulkanDevice) and
			 LoadXrFunction(s.instance, "xrGetVulkanGraphicsDevice2KHR", xrVulkan.pfnGetVulkanGraphicsDevice2))) {
		logHandler.Print("OpenXR: the Vulkan entry points of the runtime cannot be loaded\n");
		return false;
	}

	XrGraphicsRequirementsVulkan2KHR requirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };

	if (not XrSucceeded(s.instance, xrVulkan.pfnGetVulkanGraphicsRequirements2(s.instance, s.systemId, &requirements),
						"xrGetVulkanGraphicsRequirements2KHR"))
		return false;

	char text[64];

	snprintf(text, sizeof(text), "Vulkan %u.%u .. %u.%u", unsigned(XR_VERSION_MAJOR(requirements.minApiVersionSupported)),
			 unsigned(XR_VERSION_MINOR(requirements.minApiVersionSupported)), unsigned(XR_VERSION_MAJOR(requirements.maxApiVersionSupported)),
			 unsigned(XR_VERSION_MINOR(requirements.maxApiVersionSupported)));
	info = String(text);
	return true;
}


const void* GfxXRHandler::GraphicsBinding(void)
noexcept
{
	static XrGraphicsBindingVulkan2KHR binding{ XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };

	binding.instance = vkContext.m_instance;
	binding.physicalDevice = vkContext.PhysicalDevice();
	binding.device = vkContext.Device();
	binding.queueFamilyIndex = vkContext.GraphicsFamily();
	binding.queueIndex = 0;
	return &binding;
}


void GfxXRHandler::SwapchainFormats(const int64_t*& colorFormats, int& colorFormatCount, const int64_t*& depthFormats, int& depthFormatCount) const
noexcept
{
	static const int64_t colorFormatList[] = { VK_FORMAT_R8G8B8A8_SRGB,		  VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R16G16B16A16_SFLOAT,
											   VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
	static const int64_t depthFormatList[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D16_UNORM };

	colorFormats = colorFormatList;
	colorFormatCount = int(sizeof(colorFormatList) / sizeof(colorFormatList[0]));
	depthFormats = depthFormatList;
	depthFormatCount = int(sizeof(depthFormatList) / sizeof(depthFormatList[0]));
}


bool GfxXRHandler::CreateSwapchainTargets(XRSwapchain& swapchain, uint32_t imageCount, bool isDepth, const char* name)
{
	AutoArray<XrSwapchainImageVulkan2KHR> images;

	images.Resize(int32_t(imageCount));
	for (int i = 0; i < images.Length(); ++i)
		images[i] = XrSwapchainImageVulkan2KHR{ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
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

		bool ok = isDepth ? target->CreateExternal(swapchain.width, swapchain.height, String(name), swapchain.layers, VK_NULL_HANDLE,
												   VK_FORMAT_UNDEFINED, images[int32_t(i)].image, VkFormat(swapchain.format))
						  : target->CreateExternal(swapchain.width, swapchain.height, String(name), swapchain.layers, images[int32_t(i)].image,
												   VkFormat(swapchain.format));

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
	return IsSRGBFormat(baseDisplayHandler.CurrentBackBufferFormat());
}


bool GfxXRHandler::CreateVulkanInstance(const VkInstanceCreateInfo& info, VkInstance& instance)
noexcept
{
	if (not (IsAvailable() and xrVulkan.pfnCreateVulkanInstance))
		return false;

	XrVulkanInstanceCreateInfoKHR	createInfo{ XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
	VkResult						result = VK_SUCCESS;

	createInfo.systemId = m_state->systemId;
	createInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
	createInfo.vulkanCreateInfo = &info;
	if (not XrSucceeded(m_state->instance, xrVulkan.pfnCreateVulkanInstance(m_state->instance, &createInfo, &instance, &result),
						"xrCreateVulkanInstanceKHR"))
		return false;
	if (result != VK_SUCCESS) {
		logHandler.Print("OpenXR: vkCreateInstance through the runtime failed (%d)\n", int(result));
		return false;
	}
	return true;
}


bool GfxXRHandler::GetVulkanPhysicalDevice(VkInstance instance, VkPhysicalDevice& physicalDevice)
noexcept
{
	if (not (IsAvailable() and xrVulkan.pfnGetVulkanGraphicsDevice2))
		return false;

	XrVulkanGraphicsDeviceGetInfoKHR getInfo{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };

	getInfo.systemId = m_state->systemId;
	getInfo.vulkanInstance = instance;
	return XrSucceeded(m_state->instance, xrVulkan.pfnGetVulkanGraphicsDevice2(m_state->instance, &getInfo, &physicalDevice),
					   "xrGetVulkanGraphicsDevice2KHR") and
		(physicalDevice != VK_NULL_HANDLE);
}


bool GfxXRHandler::CreateVulkanDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo& info, VkDevice& device)
noexcept
{
	if (not (IsAvailable() and xrVulkan.pfnCreateVulkanDevice))
		return false;

	XrVulkanDeviceCreateInfoKHR	createInfo{ XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
	VkResult					result = VK_SUCCESS;

	createInfo.systemId = m_state->systemId;
	createInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
	createInfo.vulkanPhysicalDevice = physicalDevice;
	createInfo.vulkanCreateInfo = &info;
	if (not XrSucceeded(m_state->instance, xrVulkan.pfnCreateVulkanDevice(m_state->instance, &createInfo, &device, &result),
						"xrCreateVulkanDeviceKHR"))
		return false;
	if (result != VK_SUCCESS) {
		logHandler.Print("OpenXR: vkCreateDevice through the runtime failed (%d)\n", int(result));
		return false;
	}
	SetDeviceCreated();
	return true;
}

// =================================================================================================
