#pragma once

#include <math.h>
#include <stdlib.h>
#include <functional>

#include "vkframework.h"
#include "swapchain.h"
#include "std_defines.h"
#include "string.hpp"
#include "basesingleton.hpp"
#include "sdlhandler.h"

// =================================================================================================
// Vulkan DisplayHandler: manages the SDL window, the VkSwapchainKHR (via the Swapchain wrapper)
// and the per-back-buffer ImageLayoutTracker. Provides the same external API as the DX12 version
// (Init / Create / Update / EnableBackBuffer / DisableBackBuffer / etc.).
//
// Init order on the Vulkan path differs from DX12 because the Vulkan device needs the surface
// (carried by VKContext) before it can be created, and the swapchain in turn needs the device.
// See gfxRenderer::InitGraphics: SDL window comes first, then VKContext::Create reads it for
// the surface, then DisplayHandler::SetupSwapchain finalizes the swapchain. The legacy
// CreateSwapChain step inside SetupDisplay (DX12) is replaced by an explicit SetupSwapchain
// call after the renderer has the device up.
//
// Startup order (Vulkan caller's responsibility):
//   1. baseDisplayHandler.Init()             — SDL video init, enumerate display modes
//   2. baseDisplayHandler.Create()           — create SDL window (SDL_WINDOW_VULKAN flag)
//   3. gfxRenderer.InitGraphics()            — VkInstance + VkSurfaceKHR (from window) + VkDevice
//                                               + VMA + descriptor pool + cbv allocator
//   4. baseDisplayHandler.SetupSwapchain()   — swapchain.Create + cmdQueue.InitSyncObjects

class BaseDisplayHandler
    : public PolymorphSingleton<BaseDisplayHandler>
{
public:
    int             m_width;
    int             m_height;
    int             m_maxWidth;
    int             m_maxHeight;
    bool            m_isFullscreen;
    bool            m_vSync;
    bool            m_isLandscape;
    float           m_aspectRatio;

    SDL_Window*     m_window;

    Swapchain       m_swapchain;
    uint32_t        m_backBufferIndex { 0 };

    AutoArray<SDL_DisplayMode>      m_displayModes;
    int                             m_activeDisplayMode{ 0 };

    BaseDisplayHandler()
        : m_width(0)
        , m_height(0)
        , m_maxWidth(0)
        , m_maxHeight(0)
        , m_isFullscreen(false)
        , m_vSync(true)
        , m_isLandscape(false)
        , m_aspectRatio(1.0f)
        , m_window(nullptr)
        , m_activeDisplayMode(0)
    {
        _instance = this;
    }

    virtual ~BaseDisplayHandler();

    bool Init(void);

    int GetDisplayModes(void);

    void Create(String windowTitle = "", int width = 1920, int height = 1080,
                bool useFullscreen = true, bool vSync = false);

    // Vulkan-only: creates the swapchain after VKContext is up. Called from
    // gfxRenderer::InitGraphics once the device + surface exist.
    bool SetupSwapchain(void);

    static BaseDisplayHandler& Instance(void) {
        return dynamic_cast<BaseDisplayHandler&>(PolymorphSingleton::Instance());
    }

    int FindDisplayMode(int width, int height);

    virtual void ComputeDimensions(int width, int height, bool useFullscreen) noexcept;

    virtual void SetupDisplay(String windowTitle);

    // Present the current back buffer, advance to the next one.
    virtual void Update(void);

    void EndFrame(void);
    void BeginFrame(void);

    // Transition current back buffer PRESENT/UNDEFINED → COLOR_ATTACHMENT.
    void EnableBackBuffer(void) noexcept;

    // Transition current back buffer COLOR_ATTACHMENT → PRESENT_SRC_KHR. Call before Present.
    void DisableBackBuffer(void) noexcept;

    inline VkImage CurrentBackBuffer(void) const noexcept {
        return m_swapchain.Image(m_backBufferIndex);
    }

    inline VkImageView CurrentBackBufferView(void) const noexcept {
        return m_swapchain.ImageView(m_backBufferIndex);
    }

    inline ImageLayoutTracker& CurrentBackBufferTracker(void) noexcept {
        return m_swapchain.LayoutTracker(m_backBufferIndex);
    }

    inline int GetWidth(void) noexcept { return m_width; }

    inline int GetHeight(void) noexcept { return m_height; }

    inline float GetAspectRatio(void) noexcept { return m_aspectRatio; }

    inline SDL_Window* GetWindow(void) noexcept { return m_window; }

    inline VkSwapchainKHR SwapChain(void) noexcept {
        return m_swapchain.Handle();
    }

    inline const AutoArray<SDL_DisplayMode>& DisplayModes(void) const noexcept {
        return m_displayModes;
    }

    inline const SDL_DisplayMode& GetDisplayMode(int i = -1) const noexcept {
        return m_displayModes[((i < 0) || (i >= m_displayModes.Length())) ? m_activeDisplayMode : i];
    }

    inline int  SelectedDisplayMode(void) noexcept {
        return m_activeDisplayMode;
    }

    inline void SelectDisplayMode(int displayMode) noexcept {
        m_activeDisplayMode = displayMode;
    }

    inline bool IsFullScreen(void) noexcept {
        return m_isFullscreen;
    }

    inline void SetFullScreen(bool useFullscreen) noexcept {
        m_isFullscreen = useFullscreen;
    }

    void SwitchDisplayMode(int direction);

    void ToggleFullscreen(void);

    inline bool DisplayModeHasChanged(int& lastDisplayMode) noexcept {
        if (lastDisplayMode == m_activeDisplayMode)
            return false;
        lastDisplayMode = m_activeDisplayMode;
        return true;
    }

    bool UpdateDisplayMode(int displayMode, bool useFullscreen);

    virtual void RequestDisplayChange(int displayMode, bool useFullscreen) {}
};

#define baseDisplayHandler BaseDisplayHandler::Instance()

// =================================================================================================
