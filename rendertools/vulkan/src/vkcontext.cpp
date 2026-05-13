// VMA single-header implementation lives here (exactly one TU project-wide).
#define VMA_IMPLEMENTATION
#include "vkframework.h"

#include "vkcontext.h"
#include "array.hpp"

#include <cstdio>
#include <cstring>

// Stack-array caps for one-off Vulkan enumeration. If exceeded, Create returns false with a clear log.
static constexpr uint32_t kMaxLayers = 128;
static constexpr uint32_t kMaxInstanceExts = 32;
static constexpr uint32_t kMaxPhysicalDevices = 16;
static constexpr uint32_t kMaxQueueFamilies = 16;

// =================================================================================================
// Validation-layer callback. Buffers Vulkan validation/performance messages so that
// GfxStates::CheckError -> VKContext::DrainMessages can print them synchronously at the call site
// (1:1 mirror of DX12Context::DrainMessages). The validation layer may invoke this callback from
// a background thread, hence the mutex.

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL VkContextDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData) noexcept
{
    (void)userData;
    const char* sev = "INFO";
    bool isError = false;
    bool isWarning = false;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        sev = "ERROR";
        isError = true;
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        sev = "WARNING";
        isWarning = true;
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        sev = "INFO";
    else
        sev = "VERBOSE";

    const char* kind = "GEN";
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        kind = "PERF";
    else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        kind = "VAL";

    char header[64];
    snprintf(header, sizeof(header), "Vulkan %s/%s: ", sev, kind);

    VKContext& ctx = vkContext;
    {
        std::lock_guard<std::mutex> lock(ctx.m_validationMutex);
        VKContext::ValidationMessage msg;
        msg.text = header;
        msg.text += data ? data->pMessage : "(no msg)";
        msg.isError = isError;
        ctx.m_validationLog.push_back(std::move(msg));
        if (isError)
            ++ctx.m_validationErrorCount;
        else if (isWarning)
            ++ctx.m_validationWarningCount;
    }
    return VK_FALSE;  // never abort the offending Vulkan call
}
#endif


int VKContext::DrainMessages(bool onlyErrors) noexcept
{
#ifdef _DEBUG
    std::vector<ValidationMessage> drained;
    int errors = 0;
    {
        std::lock_guard<std::mutex> lock(m_validationMutex);
        drained.swap(m_validationLog);
        errors = m_validationErrorCount;
        m_validationErrorCount = 0;
        m_validationWarningCount = 0;
    }
    for (const ValidationMessage& msg : drained) {
        if (onlyErrors and not msg.isError)
            continue;
        fprintf(stderr, "%s\n", msg.text.c_str());
    }
    if (not drained.empty())
        fflush(stderr);
    return errors;
#else
    (void)onlyErrors;
    return 0;
#endif
}

// =================================================================================================
// VKContext::Create — sequencing helper. Each step has its own private method.

bool VKContext::Create(SDL_Window* window, bool enableValidationLayers) noexcept
{
    if (not window) {
        fprintf(stderr, "VKContext::Create: null SDL_Window\n");
        return false;
    }
    if (not CreateInstance(window, enableValidationLayers))
        return false;
#ifdef _DEBUG
    if (not RegisterDebugMessenger(enableValidationLayers))
        return false;
#endif
    if (not CreateSurface(window))
        return false;
    if (not SelectPhysicalDevice())
        return false;
    if (not SelectQueueFamilies())
        return false;
    if (not CreateDevice())
        return false;
    if (not CreateAllocator())
        return false;
    return true;
}


void VKContext::Destroy(void) noexcept
{
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if ((m_surface != VK_NULL_HANDLE) and (m_instance != VK_NULL_HANDLE)) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
#ifdef _DEBUG
    UnregisterDebugMessenger();
#endif
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    m_graphicsQueue = VK_NULL_HANDLE;
    m_presentQueue = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
}

// =================================================================================================
// CreateInstance: collects required extensions (SDL surface + optional debug-utils) and the
// validation layer (optional, debug-only), then vkCreateInstance.

bool VKContext::CreateInstance(SDL_Window* window, bool enableValidationLayers) noexcept
{
    // Required instance extensions for the SDL Vulkan surface (e.g. VK_KHR_surface, VK_KHR_win32_surface)
    uint32_t sdlExtCount = 0;
    if (SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, nullptr) == SDL_FALSE) {
        fprintf(stderr, "VKContext::CreateInstance: SDL_Vulkan_GetInstanceExtensions(count) failed: %s\n", SDL_GetError());
        return false;
    }
    if (sdlExtCount + 1 > kMaxInstanceExts) {
        fprintf(stderr, "VKContext::CreateInstance: too many SDL instance extensions (%u; max %u)\n", sdlExtCount, kMaxInstanceExts);
        return false;
    }
    StaticArray<const char*, kMaxInstanceExts> extensions { };
    if (SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, extensions.data()) == SDL_FALSE) {
        fprintf(stderr, "VKContext::CreateInstance: SDL_Vulkan_GetInstanceExtensions(list) failed: %s\n", SDL_GetError());
        return false;
    }
    uint32_t extCount = sdlExtCount;
#ifdef _DEBUG
    if (enableValidationLayers)
        extensions[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif

    // Optional validation layer — enabled only if both requested AND available on this driver.
    const char* layerName = "VK_LAYER_KHRONOS_validation";
    bool useValidation = false;
#ifdef _DEBUG
    if (enableValidationLayers and LayerAvailable(layerName))
        useValidation = true;
#else
    (void)enableValidationLayers;
#endif

    VkApplicationInfo appInfo { };
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Smiley-Battle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "rendertools";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = m_apiVersion;

    VkInstanceCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &appInfo;
    info.enabledExtensionCount = extCount;
    info.ppEnabledExtensionNames = extensions.data();
    if (useValidation) {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &layerName;
    }

    VkResult res = vkCreateInstance(&info, nullptr, &m_instance);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "VKContext::CreateInstance: vkCreateInstance failed (%d)\n", (int)res);
        return false;
    }
    return true;
}

// =================================================================================================
// CreateSurface: SDL_Vulkan_CreateSurface wraps the platform-specific surface creation
// (xlib/wayland on Linux, Win32 on Windows). The instance must have requested the right
// surface extensions — handled by SDL_Vulkan_GetInstanceExtensions in CreateInstance.

bool VKContext::CreateSurface(SDL_Window* window) noexcept
{
    if (SDL_Vulkan_CreateSurface(window, m_instance, &m_surface) == SDL_FALSE) {
        fprintf(stderr, "VKContext::CreateSurface: SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// =================================================================================================
// SelectPhysicalDevice: enumerate, score each, pick the highest. Discrete GPU > integrated >
// CPU/SW. Within tier: highest device-local heap size as a VRAM proxy.

bool VKContext::SelectPhysicalDevice(void) noexcept
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        fprintf(stderr, "VKContext::SelectPhysicalDevice: no Vulkan-capable physical device\n");
        return false;
    }
    if (count > kMaxPhysicalDevices) {
        fprintf(stderr, "VKContext::SelectPhysicalDevice: too many devices (%u; max %u), truncating\n", count, kMaxPhysicalDevices);
        count = kMaxPhysicalDevices;
    }
    StaticArray<VkPhysicalDevice, kMaxPhysicalDevices> devices { };
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < count; ++i) {
        int score = RatePhysicalDevice(devices[i]);
        if (score > bestScore) {
            bestScore = score;
            bestDevice = devices[i];
        }
    }
    if (bestDevice == VK_NULL_HANDLE) {
        fprintf(stderr, "VKContext::SelectPhysicalDevice: no suitable physical device (none meets API 1.3)\n");
        return false;
    }
    m_physicalDevice = bestDevice;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProps);
    fprintf(stderr, "Vulkan device: %s (api %u.%u.%u)\n",
            m_deviceProps.deviceName,
            VK_VERSION_MAJOR(m_deviceProps.apiVersion),
            VK_VERSION_MINOR(m_deviceProps.apiVersion),
            VK_VERSION_PATCH(m_deviceProps.apiVersion));
    return true;
}


int VKContext::RatePhysicalDevice(VkPhysicalDevice device) noexcept
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    // Reject devices that don't support our minimum API version.
    if (props.apiVersion < VK_API_VERSION_1_3)
        return -1;

    int score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000000;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        score += 100000;

    // Tie-break: device-local heap size (rough VRAM proxy in MB).
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(device, &mem);
    VkDeviceSize maxLocal = 0;
    for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
        if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            if (mem.memoryHeaps[i].size > maxLocal)
                maxLocal = mem.memoryHeaps[i].size;
        }
    }
    score += int(maxLocal / (1024 * 1024));
    return score;
}

// =================================================================================================
// SelectQueueFamilies: graphics queue (VK_QUEUE_GRAPHICS_BIT), present queue (per-family
// surface-support query). On most desktop drivers both collapse to the same family; we still
// store them separately so the same-family case is just two equal indices, no special branch.

bool VKContext::SelectQueueFamilies(void) noexcept
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count, nullptr);
    if (count == 0) {
        fprintf(stderr, "VKContext::SelectQueueFamilies: device reports zero queue families\n");
        return false;
    }
    if (count > kMaxQueueFamilies) {
        fprintf(stderr, "VKContext::SelectQueueFamilies: too many queue families (%u; max %u), truncating\n", count, kMaxQueueFamilies);
        count = kMaxQueueFamilies;
    }
    StaticArray<VkQueueFamilyProperties, kMaxQueueFamilies> props { };
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count, props.data());

    bool gfxFound = false;
    bool presentFound = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (not gfxFound and (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            m_graphicsFamily = i;
            gfxFound = true;
        }
        VkBool32 surfaceSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &surfaceSupport);
        if (not presentFound and surfaceSupport) {
            m_presentFamily = i;
            presentFound = true;
        }
        if (gfxFound and presentFound)
            break;
    }
    if (not gfxFound) {
        fprintf(stderr, "VKContext::SelectQueueFamilies: no graphics queue family\n");
        return false;
    }
    if (not presentFound) {
        fprintf(stderr, "VKContext::SelectQueueFamilies: no present-capable queue family\n");
        return false;
    }
    return true;
}

// =================================================================================================
// CreateDevice: requires VK_KHR_swapchain (device extension), enables Vulkan 1.3 features
// (dynamicRendering, synchronization2, shaderDemoteToHelperInvocation). One queue per distinct
// family — collapse to a single queueCreateInfo when graphicsFamily == presentFamily.

bool VKContext::CreateDevice(void) noexcept
{
    const float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueInfos[2] { };
    uint32_t queueInfoCount = 0;

    queueInfos[queueInfoCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfos[queueInfoCount].queueFamilyIndex = m_graphicsFamily;
    queueInfos[queueInfoCount].queueCount = 1;
    queueInfos[queueInfoCount].pQueuePriorities = &queuePriority;
    ++queueInfoCount;

    if (m_presentFamily != m_graphicsFamily) {
        queueInfos[queueInfoCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[queueInfoCount].queueFamilyIndex = m_presentFamily;
        queueInfos[queueInfoCount].queueCount = 1;
        queueInfos[queueInfoCount].pQueuePriorities = &queuePriority;
        ++queueInfoCount;
    }

    VkPhysicalDeviceVulkan13Features feats13 { };
    feats13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    feats13.dynamicRendering = VK_TRUE;
    feats13.synchronization2 = VK_TRUE;
    // Required by SPIR-V emitted from HLSL `discard` (DXC uses OpDemoteToHelperInvocation).
    feats13.shaderDemoteToHelperInvocation = VK_TRUE;

    // VK_EXT_dynamic_rendering_unused_attachments — relax the Vulkan strictness that
    // pipeline colorAttachmentCount must equal the active render-pass colorAttachmentCount.
    // We need this for the DX12-style pattern: the same RT scope (e.g. SceneBuffer with 3
    // color attachments) hosts shaders that write fewer SV_Targets (e.g. DecalShader with 1).
    VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT featsUnusedAtt { };
    featsUnusedAtt.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT;
    featsUnusedAtt.dynamicRenderingUnusedAttachments = VK_TRUE;
    feats13.pNext = &featsUnusedAtt;

    // Allows vkCmdPipelineBarrier2 inside an active dynamic-rendering instance.
    // Required by DecalHandler::Render's intra-renderpass SetMemoryBarrier between
    // the two-pass mask/draw sequence.
    VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR featsLocalRead { };
    featsLocalRead.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR;
    featsLocalRead.dynamicRenderingLocalRead = VK_TRUE;
    featsUnusedAtt.pNext = &featsLocalRead;

    // Core 1.0 features. samplerAnisotropy is needed by TiledTexture (max 16).
    // fragmentStoresAndAtomics enables RWTexture2D + InterlockedMin in the fragment
    // stage (used by DecalShader's two-pass depth mask).
    VkPhysicalDeviceFeatures features { };
    features.samplerAnisotropy = VK_TRUE;
    features.fragmentStoresAndAtomics = VK_TRUE;

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME,
    };

    VkDeviceCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = &feats13;
    info.queueCreateInfoCount = queueInfoCount;
    info.pQueueCreateInfos = queueInfos;
    info.enabledExtensionCount = (uint32_t)(sizeof(deviceExtensions) / sizeof(deviceExtensions[0]));
    info.ppEnabledExtensionNames = deviceExtensions;
    info.pEnabledFeatures = &features;

    VkResult res = vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "VKContext::CreateDevice: vkCreateDevice failed (%d)\n", (int)res);
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
    return true;
}

// =================================================================================================
// CreateAllocator: VMA configuration. Vulkan 1.3 minimum — VMA picks up the new APIs
// (Maintenance5, host-image-copy, etc. when available).

bool VKContext::CreateAllocator(void) noexcept
{
    VmaAllocatorCreateInfo info { };
    info.physicalDevice = m_physicalDevice;
    info.device = m_device;
    info.instance = m_instance;
    info.vulkanApiVersion = m_apiVersion;

    VkResult res = vmaCreateAllocator(&info, &m_allocator);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "VKContext::CreateAllocator: vmaCreateAllocator failed (%d)\n", (int)res);
        return false;
    }
    return true;
}

// =================================================================================================
// Validation-layer registration (debug-only). VK_EXT_debug_utils functions are not in the
// Vulkan core — we must look them up via vkGetInstanceProcAddr.

#ifdef _DEBUG

bool VKContext::RegisterDebugMessenger(bool enableValidationLayers) noexcept
{
    if (not enableValidationLayers)
        return true;

    m_pfnCreateDebugMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    m_pfnDestroyDebugMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
    if (not m_pfnCreateDebugMessenger or not m_pfnDestroyDebugMessenger) {
        fprintf(stderr, "VKContext::RegisterDebugMessenger: VK_EXT_debug_utils entry points not found\n");
        return true;  // not fatal — instance is up, we just won't get callbacks
    }

    VkDebugUtilsMessengerCreateInfoEXT info { };
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = VkContextDebugCallback;

    VkResult res = m_pfnCreateDebugMessenger(m_instance, &info, nullptr, &m_debugMessenger);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "VKContext::RegisterDebugMessenger: create failed (%d)\n", (int)res);
        return true;  // not fatal
    }
    return true;
}


void VKContext::UnregisterDebugMessenger(void) noexcept
{
    if ((m_debugMessenger != VK_NULL_HANDLE) and m_pfnDestroyDebugMessenger) {
        m_pfnDestroyDebugMessenger(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    m_pfnCreateDebugMessenger = nullptr;
    m_pfnDestroyDebugMessenger = nullptr;
}

#endif

// =================================================================================================
// LayerAvailable: scan vkEnumerateInstanceLayerProperties for the given layer name.

bool VKContext::LayerAvailable(const char* name) noexcept
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    if (count == 0)
        return false;
    if (count > kMaxLayers) {
        fprintf(stderr, "VKContext::LayerAvailable: too many layers (%u; max %u), truncating\n", count, kMaxLayers);
        count = kMaxLayers;
    }
    StaticArray<VkLayerProperties, kMaxLayers> layers { };
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(layers[i].layerName, name) == 0)
            return true;
    }
    return false;
}

// =================================================================================================
