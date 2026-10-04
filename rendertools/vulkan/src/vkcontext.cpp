// VMA single-header implementation lives here (exactly one TU project-wide).
#define VMA_IMPLEMENTATION
#include "vkframework.h"

#include "vkcontext.h"
#include "shader_compiler.h"
#include "acceleration_structure.h"
#include "vk13api.h"
#include "array.hpp"
#include "loghandler.h"

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

#if ENABLE_VK_LOGGING
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

    char header[160];

    VKContext& ctx = vkContext;
    {
        std::lock_guard<std::mutex> lock(ctx.m_validationMutex);
        if (ctx.m_validationShader[0] != '\0')
            snprintf(header, sizeof(header), "Vulkan %s/%s [shader '%s']: ", sev, kind, ctx.m_validationShader);
        else
            snprintf(header, sizeof(header), "Vulkan %s/%s: ", sev, kind);
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
    // Print errors/warnings immediately so they surface even when GfxStates::CheckError ->
    // DrainMessages is not called in the render path. Synchronous on the offending call's thread,
    // so the message lands right at the bad vkCmd. The buffer stays intact for DrainMessages.
    if (isError or isWarning) {
        logHandler.Print("%s%s\n", header, data ? data->pMessage : "(no msg)");
    }
    return VK_FALSE;  // never abort the offending Vulkan call
}
#endif


int VKContext::DrainMessages(bool onlyErrors) noexcept
{
#if ENABLE_VK_LOGGING
    std::vector<ValidationMessage> drained;
    int errors = 0;
    
    std::lock_guard<std::mutex> lock(m_validationMutex);
    drained.swap(m_validationLog);
    errors = m_validationErrorCount;
    m_validationErrorCount = 0;
    m_validationWarningCount = 0;
    
    for (const ValidationMessage& msg : drained) {
        if (onlyErrors and not msg.isError)
            continue;
        logHandler.Print("%s\n", msg.text.c_str());
        ++errors;
    }
    return errors;
#else
    (void)onlyErrors;
    return 0;
#endif
}


void VKContext::SetValidationShader(const char* name) noexcept
{
#if ENABLE_VK_LOGGING
    if (name == nullptr)
        name = "";
    if (std::strcmp(m_validationShader, name) == 0)
        return;
    std::lock_guard<std::mutex> lock(m_validationMutex);
    snprintf(m_validationShader, sizeof(m_validationShader), "%s", name);
#else
    (void)name;
#endif
}

// =================================================================================================
// VKContext::Create — sequencing helper. Each step has its own private method.

bool VKContext::Create(SDL_Window* window, bool enableValidationLayers, const GfxFeatureRequest& request) noexcept
{
    if (not window) {
        logHandler.Print("VKContext::Create: null SDL_Window\n");
        return false;
    }
    if (not CreateInstance(window, enableValidationLayers))
        return false;
#if ENABLE_VK_LOGGING
    if (not RegisterDebugMessenger(enableValidationLayers))
        return false;
#endif
    if (not CreateSurface(window))
        return false;
    if (not SelectPhysicalDevice(request))
        return false;
    if (not SelectQueueFamilies())
        return false;
    if (not CreateDevice(request))
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
#if ENABLE_VK_LOGGING
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
        logHandler.Print("VKContext::CreateInstance: SDL_Vulkan_GetInstanceExtensions(count) failed: %s\n", SDL_GetError());
        return false;
    }
    if (sdlExtCount + 1 > kMaxInstanceExts) {
        logHandler.Print("VKContext::CreateInstance: too many SDL instance extensions (%u; max %u)\n", sdlExtCount, kMaxInstanceExts);
        return false;
    }
    StaticArray<const char*, kMaxInstanceExts> extensions { };
    if (SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, extensions.data()) == SDL_FALSE) {
        logHandler.Print("VKContext::CreateInstance: SDL_Vulkan_GetInstanceExtensions(list) failed: %s\n", SDL_GetError());
        return false;
    }
    uint32_t extCount = sdlExtCount;
#if ENABLE_VK_LOGGING
    if (enableValidationLayers)
        extensions[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif

    // Optional validation layer — enabled only if both requested AND available on this driver.
    const char* layerName = "VK_LAYER_KHRONOS_validation";
    bool useValidation = false;
#if ENABLE_VK_LOGGING
    if (enableValidationLayers and LayerAvailable(layerName))
        useValidation = true;
#else
    (void)enableValidationLayers;
#endif

    VkApplicationInfo appInfo { };
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = SDL_GetWindowTitle(window);
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
        logHandler.Print("VKContext::CreateInstance: vkCreateInstance failed (%d)\n", (int)res);
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
        logHandler.Print("VKContext::CreateSurface: SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// =================================================================================================
// SelectPhysicalDevice: enumerate, score each, pick the highest. Discrete GPU > integrated >
// CPU/SW. Within tier: highest device-local heap size as a VRAM proxy.

bool VKContext::SelectPhysicalDevice(const GfxFeatureRequest& request) noexcept
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        logHandler.Print("VKContext::SelectPhysicalDevice: no Vulkan-capable physical device\n");
        return false;
    }
    if (count > kMaxPhysicalDevices) {
#ifdef _DEBUG
        logHandler.Print("VKContext::SelectPhysicalDevice: too many devices (%u; max %u), truncating\n", count, kMaxPhysicalDevices);
#endif
        count = kMaxPhysicalDevices;
    }
    StaticArray<VkPhysicalDevice, kMaxPhysicalDevices> devices { };
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < count; ++i) {
        int score = RatePhysicalDevice(devices[i], request);
        if (score > bestScore) {
            bestScore = score;
            bestDevice = devices[i];
        }
    }
    if (bestDevice == VK_NULL_HANDLE) {
        logHandler.Print("VKContext::SelectPhysicalDevice: no suitable physical device (API 1.2 with extensions or 1.3, plus the required features)\n");
        return false;
    }
    m_physicalDevice = bestDevice;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProps);
    DeviceSupport support = QueryDeviceSupport(m_physicalDevice);
    m_apiVersion = support.apiVersion;
    m_availableFeatures = support.features;
#ifdef _DEBUG
    logHandler.Print("Vulkan device: %s (api %u.%u.%u, using %s)\n",
            m_deviceProps.deviceName,
            VK_VERSION_MAJOR(m_deviceProps.apiVersion),
            VK_VERSION_MINOR(m_deviceProps.apiVersion),
            VK_VERSION_PATCH(m_deviceProps.apiVersion),
            UsesCore13() ? "1.3 core" : "1.2 + extensions");
#endif
    return true;
}


bool VKContext::QueryDeviceExtensions(VkPhysicalDevice device, AutoArray<VkExtensionProperties>& extensions) noexcept
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;
    extensions.Resize(int32_t(count));
    if (count == 0)
        return true;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.Data()) != VK_SUCCESS)
        return false;
    extensions.Resize(int32_t(count));
    return true;
}


bool VKContext::HasDeviceExtension(const AutoArray<VkExtensionProperties>& extensions, const char* name) noexcept
{
    for (int32_t i = 0; i < extensions.Length(); ++i) {
        if (std::strcmp(extensions[i].extensionName, name) == 0)
            return true;
    }
    return false;
}


VKContext::DeviceSupport VKContext::QueryDeviceSupport(VkPhysicalDevice device) noexcept
{
    DeviceSupport support;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);
    uint32_t major = VK_VERSION_MAJOR(props.apiVersion);
    uint32_t minor = VK_VERSION_MINOR(props.apiVersion);
    support.apiVersion = VK_MAKE_API_VERSION(0, major, minor, 0);
    if (support.apiVersion > VK_API_VERSION_1_3)
        support.apiVersion = VK_API_VERSION_1_3;
    if (support.apiVersion < VK_API_VERSION_1_2)
        return support;

    AutoArray<VkExtensionProperties> extensions;
    if (not QueryDeviceExtensions(device, extensions))
        return support;
    const bool core13 = support.apiVersion >= VK_API_VERSION_1_3;
    const bool hasDynamicRenderingExt = HasDeviceExtension(extensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    const bool hasSync2Ext = HasDeviceExtension(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    const bool hasDemoteExt = HasDeviceExtension(extensions, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
    const bool hasEdsExt = HasDeviceExtension(extensions, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
    const bool hasEds2Ext = HasDeviceExtension(extensions, VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
    const bool hasUnusedAttExt = HasDeviceExtension(extensions, VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME);
    const bool hasLocalReadExt = HasDeviceExtension(extensions, VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME);
    const bool hasSwapchainExt = HasDeviceExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    VkPhysicalDeviceFeatures2 features { };
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    VkPhysicalDeviceVulkan12Features feats12 { };
    feats12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features feats13 { };
    feats13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR featsDynamicRendering { };
    featsDynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    VkPhysicalDeviceSynchronization2FeaturesKHR featsSync2 { };
    featsSync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT featsDemote { };
    featsDemote.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT featsEds { };
    featsEds.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT featsEds2 { };
    featsEds2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
    VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT featsUnusedAtt { };
    featsUnusedAtt.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT;
    VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR featsLocalRead { };
    featsLocalRead.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR;

    void** chain = &features.pNext;
    auto append = [&chain](auto& feature) {
        *chain = &feature;
        chain = &feature.pNext;
    };
    append(feats12);
    if (core13)
        append(feats13);
    else {
        if (hasDynamicRenderingExt)
            append(featsDynamicRendering);
        if (hasSync2Ext)
            append(featsSync2);
        if (hasDemoteExt)
            append(featsDemote);
        if (hasEdsExt)
            append(featsEds);
        if (hasEds2Ext)
            append(featsEds2);
    }
    if (hasUnusedAttExt)
        append(featsUnusedAtt);
    if (hasLocalReadExt)
        append(featsLocalRead);
    vkGetPhysicalDeviceFeatures2(device, &features);

    if (core13)
        support.isUsable = feats13.dynamicRendering and feats13.synchronization2 and feats13.shaderDemoteToHelperInvocation;
    else
        support.isUsable = featsDynamicRendering.dynamicRendering and featsSync2.synchronization2 and
                           featsDemote.shaderDemoteToHelperInvocation and featsEds.extendedDynamicState and
                           featsEds2.extendedDynamicState2;
    support.isUsable = support.isUsable and hasSwapchainExt;

    const VkPhysicalDeviceFeatures& core = features.features;
    auto set = [&support](GfxFeature feature, bool isAvailable) {
        if (isAvailable)
            support.features |= GfxFeatureBit(feature);
    };
    set(GfxFeature::GeometryShader, core.geometryShader);
    set(GfxFeature::Tessellation, core.tessellationShader);
    set(GfxFeature::BlockCompression, core.textureCompressionBC);
    set(GfxFeature::Wireframe, core.fillModeNonSolid);
    set(GfxFeature::DepthClamp, core.depthClamp);
    set(GfxFeature::IndependentBlend, core.independentBlend);
    set(GfxFeature::Anisotropy, core.samplerAnisotropy);
    set(GfxFeature::StorageInVertexStage, core.vertexPipelineStoresAndAtomics);
    set(GfxFeature::StorageInFragmentStage, core.fragmentStoresAndAtomics);
    set(GfxFeature::ScalarBlockLayout, feats12.scalarBlockLayout);
    set(GfxFeature::RenderingLocalRead, hasLocalReadExt and featsLocalRead.dynamicRenderingLocalRead);
    set(GfxFeature::UnusedAttachments, hasUnusedAttExt and featsUnusedAtt.dynamicRenderingUnusedAttachments);
    set(GfxFeature::RayTracing, SupportsRayTracing(device));
    set(GfxFeature::PipelineLibrary, SupportsPipelineLibrary(device));
    return support;
}


int VKContext::RatePhysicalDevice(VkPhysicalDevice device, const GfxFeatureRequest& request) noexcept
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    DeviceSupport support = QueryDeviceSupport(device);
    if (not support.isUsable)
        return -1;
    if ((request.required & ~support.features) != 0) {
        for (uint32_t i = 0; i < uint32_t(GfxFeature::Count); ++i) {
            if ((request.required & ~support.features) & GfxFeatureBit(GfxFeature(i)))
                logHandler.Print("Vulkan device %s lacks required feature: %s\n", props.deviceName, GfxFeatureName(GfxFeature(i)));
        }
        return -1;
    }

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
        logHandler.Print("VKContext::SelectQueueFamilies: device reports zero queue families\n");
        return false;
    }
    if (count > kMaxQueueFamilies) {
#ifdef _DEBUG
        logHandler.Print("VKContext::SelectQueueFamilies: too many queue families (%u; max %u), truncating\n", count, kMaxQueueFamilies);
#endif
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
        logHandler.Print("VKContext::SelectQueueFamilies: no graphics queue family\n");
        return false;
    }
    if (not presentFound) {
        logHandler.Print("VKContext::SelectQueueFamilies: no present-capable queue family\n");
        return false;
    }
    return true;
}

// =================================================================================================
// CreateDevice: requires VK_KHR_swapchain (device extension), enables dynamicRendering, synchronization2,
// shaderDemoteToHelperInvocation (1.3 core, or on 1.2 the KHR/EXT extensions plus extended dynamic state 1+2)
// and exactly the requested GfxFeatures the device has. One queue per distinct
// family — collapse to a single queueCreateInfo when graphicsFamily == presentFamily.

bool VKContext::CreateDevice(const GfxFeatureRequest& request) noexcept
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

    m_features = request.Requested() & m_availableFeatures;
    m_hasRayTracing = HasFeature(GfxFeature::RayTracing) and ShaderCompiler::SupportsRayQuery();
    if (not m_hasRayTracing)
        m_features &= ~GfxFeatureBit(GfxFeature::RayTracing);
    m_hasPipelineLibrary = HasFeature(GfxFeature::PipelineLibrary);
    const bool core13 = UsesCore13();

    void* featureChain = nullptr;
    void** chain = &featureChain;
    auto append = [&chain](auto& feature) {
        *chain = &feature;
        chain = &feature.pNext;
    };

    const char* deviceExtensions[16] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    uint32_t deviceExtensionCount = 1;

    VkPhysicalDeviceVulkan13Features feats13 { };
    feats13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    feats13.dynamicRendering = VK_TRUE;
    feats13.synchronization2 = VK_TRUE;
    // Required by SPIR-V emitted from HLSL `discard` (DXC uses OpDemoteToHelperInvocation).
    feats13.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR featsDynamicRendering { };
    featsDynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    featsDynamicRendering.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceSynchronization2FeaturesKHR featsSync2 { };
    featsSync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    featsSync2.synchronization2 = VK_TRUE;
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT featsDemote { };
    featsDemote.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
    featsDemote.shaderDemoteToHelperInvocation = VK_TRUE;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT featsEds { };
    featsEds.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    featsEds.extendedDynamicState = VK_TRUE;
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT featsEds2 { };
    featsEds2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
    featsEds2.extendedDynamicState2 = VK_TRUE;

    if (core13)
        append(feats13);
    else {
        append(featsDynamicRendering);
        append(featsSync2);
        append(featsDemote);
        append(featsEds);
        append(featsEds2);
        deviceExtensions[deviceExtensionCount++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
        deviceExtensions[deviceExtensionCount++] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
        deviceExtensions[deviceExtensionCount++] = VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME;
        deviceExtensions[deviceExtensionCount++] = VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME;
        deviceExtensions[deviceExtensionCount++] = VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME;
    }

    // VK_EXT_dynamic_rendering_unused_attachments — relax the Vulkan strictness that
    // pipeline colorAttachmentCount must equal the active render-pass colorAttachmentCount.
    // We need this for the DX12-style pattern: the same RT scope (e.g. SceneBuffer with 3
    // color attachments) hosts shaders that write fewer SV_Targets (e.g. DecalShader with 1).
    VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT featsUnusedAtt { };
    featsUnusedAtt.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT;
    featsUnusedAtt.dynamicRenderingUnusedAttachments = VK_TRUE;
    if (HasFeature(GfxFeature::UnusedAttachments)) {
        append(featsUnusedAtt);
        deviceExtensions[deviceExtensionCount++] = VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME;
    }

    // Allows vkCmdPipelineBarrier2 inside an active dynamic-rendering instance.
    // Required by DecalHandler::Render's intra-renderpass SetMemoryBarrier between
    // the two-pass mask/draw sequence.
    VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR featsLocalRead { };
    featsLocalRead.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR;
    featsLocalRead.dynamicRenderingLocalRead = VK_TRUE;
    if (HasFeature(GfxFeature::RenderingLocalRead)) {
        append(featsLocalRead);
        deviceExtensions[deviceExtensionCount++] = VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME;
    }

    // scalarBlockLayout: the particle StructuredBuffers (Particle = 36 B, ParticleSystemParams = 108 B)
    // are tightly packed to match the C++ upload structs (compiled with -fvk-use-dx-layout), so their
    // array stride is not a multiple of 16. Standard/relaxed storage-buffer layout rejects that — a
    // float3 member forces 16-byte stride alignment — making vkCreateShaderModule fail spirv-val on the
    // particle sim/draw shaders. Scalar block layout permits the tight stride (= the C++ memory layout).
    VkPhysicalDeviceVulkan12Features feats12 { };
    feats12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    feats12.scalarBlockLayout = HasFeature(GfxFeature::ScalarBlockLayout) ? VK_TRUE : VK_FALSE;
#if USE_TRACY
    feats12.hostQueryReset = VK_TRUE;
#endif
    append(feats12);

    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT featsPipelineLibrary { };
    featsPipelineLibrary.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT;
    featsPipelineLibrary.graphicsPipelineLibrary = VK_TRUE;
    if (m_hasPipelineLibrary) {
        append(featsPipelineLibrary);
        deviceExtensions[deviceExtensionCount++] = VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME;
        deviceExtensions[deviceExtensionCount++] = VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME;
    }

    VkPhysicalDeviceAccelerationStructureFeaturesKHR featsAccelStruct { };
    featsAccelStruct.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    featsAccelStruct.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayQueryFeaturesKHR featsRayQuery { };
    featsRayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    featsRayQuery.rayQuery = VK_TRUE;

    if (m_hasRayTracing) {
        feats12.bufferDeviceAddress = VK_TRUE;
        append(featsAccelStruct);
        append(featsRayQuery);
        deviceExtensions[deviceExtensionCount++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        deviceExtensions[deviceExtensionCount++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        deviceExtensions[deviceExtensionCount++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
    }

    // Core 1.0 features. samplerAnisotropy is needed by TiledTexture (max 16).
    // fragmentStoresAndAtomics enables RWTexture2D + InterlockedMin in the fragment
    // stage (used by DecalShader's two-pass depth mask).
    // vertexPipelineStoresAndAtomics lets the vertex stage access UAV-style storage buffers
    // (RWStructuredBuffer particles/systems/activeSlots) — required by the particle draw's
    // vertex-pulling VS; without it vkCreateGraphicsPipelines rejects the SPIR-V (the storage
    // buffers are not NonWritable).
    // independentBlend allows per-attachment blend states in one pipeline — required by WBOIT,
    // whose MRT pass blends RT0 additively (accum) and RT1 multiplicatively (revealage).
    auto enable = [this](GfxFeature feature) -> VkBool32 {
        return HasFeature(feature) ? VK_TRUE : VK_FALSE;
    };
    VkPhysicalDeviceFeatures features { };
    features.samplerAnisotropy = enable(GfxFeature::Anisotropy);
    features.textureCompressionBC = enable(GfxFeature::BlockCompression);   // BC1/BC4/BC5/BC7 for skybox + material DDS textures (universal on desktop/Xbox)
    features.fragmentStoresAndAtomics = enable(GfxFeature::StorageInFragmentStage);
    features.vertexPipelineStoresAndAtomics = enable(GfxFeature::StorageInVertexStage);
    features.independentBlend = enable(GfxFeature::IndependentBlend);
    features.fillModeNonSolid = enable(GfxFeature::Wireframe);       // VK_POLYGON_MODE_LINE for GfxStates::SetFillMode (Wireframe); universal on desktop
    features.tessellationShader = enable(GfxFeature::Tessellation);
    features.geometryShader = enable(GfxFeature::GeometryShader);
    features.depthClamp = enable(GfxFeature::DepthClamp);

    VkDeviceCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = featureChain;
    info.queueCreateInfoCount = queueInfoCount;
    info.pQueueCreateInfos = queueInfos;
    info.enabledExtensionCount = deviceExtensionCount;
    info.ppEnabledExtensionNames = deviceExtensions;
    info.pEnabledFeatures = &features;

    VkResult res = vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device);
    if (res != VK_SUCCESS) {
        logHandler.Print("VKContext::CreateDevice: vkCreateDevice failed (%d)\n", (int)res);
        return false;
    }
    if (not Vk13Api::Load(m_device, core13)) {
        logHandler.Print("VKContext::CreateDevice: cannot load the dynamic rendering / synchronization2 entry points\n");
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
    if (m_hasRayTracing and not RayTracingApi::Load(m_device, m_physicalDevice)) {
        m_hasRayTracing = false;
        m_features &= ~GfxFeatureBit(GfxFeature::RayTracing);
    }
#ifdef _DEBUG
    logHandler.Print("Vulkan ray tracing: %s\n", m_hasRayTracing ? "available (ray query)" : "not available");
#endif
    return true;
}


bool VKContext::SupportsPipelineLibrary(VkPhysicalDevice device) noexcept
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;
    AutoArray<VkExtensionProperties> extensions;
    extensions.Resize(int32_t(count));
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.Data()) != VK_SUCCESS)
        return false;
    bool hasPipelineLibrary = false;
    bool hasGraphicsPipelineLibrary = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (std::strcmp(extensions[i].extensionName, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)
            hasPipelineLibrary = true;
        else if (std::strcmp(extensions[i].extensionName, VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)
            hasGraphicsPipelineLibrary = true;
    }
    if (not (hasPipelineLibrary and hasGraphicsPipelineLibrary))
        return false;
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT pipelineLibraryFeatures { };
    pipelineLibraryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT;
    VkPhysicalDeviceFeatures2 features { };
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &pipelineLibraryFeatures;
    vkGetPhysicalDeviceFeatures2(device, &features);
    return pipelineLibraryFeatures.graphicsPipelineLibrary == VK_TRUE;
}


bool VKContext::SupportsRayTracing(VkPhysicalDevice device) noexcept
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;
    AutoArray<VkExtensionProperties> extensions;
    extensions.Resize(int32_t(count));
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.Data()) != VK_SUCCESS)
        return false;
    bool hasAccelStruct = false;
    bool hasRayQuery = false;
    bool hasDeferredOps = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (std::strcmp(extensions[i].extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
            hasAccelStruct = true;
        else if (std::strcmp(extensions[i].extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
            hasRayQuery = true;
        else if (std::strcmp(extensions[i].extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0)
            hasDeferredOps = true;
    }
    if (not (hasAccelStruct and hasRayQuery and hasDeferredOps))
        return false;

    VkPhysicalDeviceVulkan12Features features12 { };
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures { };
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.pNext = &features12;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures { };
    accelStructFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelStructFeatures.pNext = &rayQueryFeatures;
    VkPhysicalDeviceFeatures2 features { };
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &accelStructFeatures;
    vkGetPhysicalDeviceFeatures2(device, &features);
    return (accelStructFeatures.accelerationStructure == VK_TRUE) and (rayQueryFeatures.rayQuery == VK_TRUE) and
           (features12.bufferDeviceAddress == VK_TRUE);
}

// =================================================================================================
// CreateAllocator: VMA configuration with the device's API version (1.2 or 1.3) — VMA picks up the new APIs
// (Maintenance5, host-image-copy, etc. when available).

bool VKContext::CreateAllocator(void) noexcept
{
    VmaAllocatorCreateInfo info { };
    info.physicalDevice = m_physicalDevice;
    info.device = m_device;
    info.instance = m_instance;
    info.vulkanApiVersion = m_apiVersion;
    if (m_hasRayTracing)
        info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VkResult res = vmaCreateAllocator(&info, &m_allocator);
    if (res != VK_SUCCESS) {
        logHandler.Print("VKContext::CreateAllocator: vmaCreateAllocator failed (%d)\n", (int)res);
        return false;
    }
    return true;
}

// =================================================================================================
// Validation-layer registration (gated by ENABLE_VK_LOGGING). VK_EXT_debug_utils functions are
// not in the Vulkan core — we must look them up via vkGetInstanceProcAddr.

#if ENABLE_VK_LOGGING

bool VKContext::RegisterDebugMessenger(bool enableValidationLayers) noexcept
{
    if (not enableValidationLayers)
        return true;

    m_pfnCreateDebugMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    m_pfnDestroyDebugMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
    if (not m_pfnCreateDebugMessenger or not m_pfnDestroyDebugMessenger) {
        logHandler.Print("VKContext::RegisterDebugMessenger: VK_EXT_debug_utils entry points not found\n");
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
        logHandler.Print("VKContext::RegisterDebugMessenger: create failed (%d)\n", (int)res);
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
#ifdef _DEBUG
        logHandler.Print("VKContext::LayerAvailable: too many layers (%u; max %u), truncating\n", count, kMaxLayers);
#endif
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
