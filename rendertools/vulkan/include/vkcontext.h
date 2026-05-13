#pragma once

// Toggle Vulkan validation-layer logging independently of _DEBUG. Set to 1 to register the
// VK_EXT_debug_utils messenger and route validation/performance messages through DrainMessages,
// 0 to compile the logging path out entirely. Useful for diagnosing release-build crashes
// without flipping the whole _DEBUG flag.
#ifndef ENABLE_VK_LOGGING
#define ENABLE_VK_LOGGING 1
#endif

#include "vkframework.h"
#include "basesingleton.hpp"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL.h"
#include "SDL_vulkan.h"
#pragma warning(pop)

#if ENABLE_VK_LOGGING
#include <mutex>
#include <string>
#include <vector>
#endif

// =================================================================================================
// VKContext: Singleton holding the Vulkan instance, surface, physical/logical device, queues
// and VMA allocator. Created from Application::InitGraphics() AFTER displayHandler.Create()
// has produced an SDL_Window — the surface is required for queue-family selection
// (vkGetPhysicalDeviceSurfaceSupportKHR).
//
// Direct DX12Context analogue, plus two responsibilities DX12 doesn't have:
//   - Surface (DXGI uses the HWND directly, no surface object needed)
//   - VMA allocator (D3D12 has its own committed-resource allocation path)

class VKContext : public BaseSingleton<VKContext>
{
public:
    VkInstance       m_instance        { VK_NULL_HANDLE };
    VkSurfaceKHR     m_surface         { VK_NULL_HANDLE };
    VkPhysicalDevice m_physicalDevice  { VK_NULL_HANDLE };
    VkDevice         m_device          { VK_NULL_HANDLE };
    VkQueue          m_graphicsQueue   { VK_NULL_HANDLE };
    VkQueue          m_presentQueue    { VK_NULL_HANDLE };
    uint32_t         m_graphicsFamily  { 0 };
    uint32_t         m_presentFamily   { 0 };
    VmaAllocator     m_allocator       { VK_NULL_HANDLE };

    VkPhysicalDeviceProperties m_deviceProps  { };
    uint32_t                   m_apiVersion   { VK_API_VERSION_1_3 };

#if ENABLE_VK_LOGGING
    VkDebugUtilsMessengerEXT             m_debugMessenger           { VK_NULL_HANDLE };
    PFN_vkCreateDebugUtilsMessengerEXT   m_pfnCreateDebugMessenger  { nullptr };
    PFN_vkDestroyDebugUtilsMessengerEXT  m_pfnDestroyDebugMessenger { nullptr };

    // Synchronous error log filled by VkContextDebugCallback (validation layer threads can call
    // back asynchronously, so the buffer is mutex-guarded). DrainMessages flushes it to stderr
    // and resets the counters — that is the central point queried by GfxStates::CheckError.
    struct ValidationMessage {
        std::string text;
        bool        isError;
    };
    mutable std::mutex            m_validationMutex;
    std::vector<ValidationMessage> m_validationLog;
    int                           m_validationErrorCount    { 0 };
    int                           m_validationWarningCount  { 0 };
#endif

    // Creates instance, surface, picks physical device, creates logical device + queues + VMA.
    // The SDL window must already exist (renderer.HasDirectX() takes the other path in InitGraphics).
    bool Create(SDL_Window* window, bool enableValidationLayers = false) noexcept;

    void Destroy(void) noexcept;

    // m_instance is public (above) — no Instance() getter, that name collides with the
    // BaseSingleton<VKContext>::Instance() static member (Singleton accessor) and breaks
    // the #define vkContext expansion below.
    inline VkSurfaceKHR Surface(void) const noexcept { return m_surface; }
    inline VkPhysicalDevice PhysicalDevice(void) const noexcept { return m_physicalDevice; }
    inline VkDevice Device(void) const noexcept { return m_device; }
    inline VkQueue GraphicsQueue(void) const noexcept { return m_graphicsQueue; }
    inline VkQueue PresentQueue(void) const noexcept { return m_presentQueue; }
    inline uint32_t GraphicsFamily(void) const noexcept { return m_graphicsFamily; }
    inline uint32_t PresentFamily(void) const noexcept { return m_presentFamily; }
    inline VmaAllocator Allocator(void) const noexcept { return m_allocator; }
    inline uint32_t ApiVersion(void) const noexcept { return m_apiVersion; }

    inline const VkPhysicalDeviceProperties& DeviceProps(void) const noexcept { return m_deviceProps; }

    // Flush the validation-layer log to stderr and return the number of error-severity entries
    // since the last call. onlyErrors=true suppresses warnings/info from the printout (counters
    // are reset either way). When ENABLE_VK_LOGGING=0 this compiles to a no-op returning 0.
    int DrainMessages(bool onlyErrors = false) noexcept;

private:
    bool CreateInstance(SDL_Window* window, bool enableValidationLayers) noexcept;
    bool CreateSurface(SDL_Window* window) noexcept;
    bool SelectPhysicalDevice(void) noexcept;
    bool SelectQueueFamilies(void) noexcept;
    bool CreateDevice(void) noexcept;
    bool CreateAllocator(void) noexcept;

#if ENABLE_VK_LOGGING
    bool RegisterDebugMessenger(bool enableValidationLayers) noexcept;
    void UnregisterDebugMessenger(void) noexcept;
#endif

    static bool LayerAvailable(const char* name) noexcept;
    static int RatePhysicalDevice(VkPhysicalDevice device) noexcept;
};

#define vkContext VKContext::Instance()

// =================================================================================================
