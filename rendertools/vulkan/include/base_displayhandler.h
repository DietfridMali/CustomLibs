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

class CommandList;

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
    bool            m_isInRendering { false };  // active vkCmdBeginRendering scope on the back buffer
    // The command buffer the back buffer's rendering scope was opened on. vkCmdEndRendering has to go
    // into the same one, and by the time it is issued another list may well be the current one.
    VkCommandBuffer m_backBufferCb { VK_NULL_HANDLE };
    // The list the back buffer brings along when nobody else has one open. A render target creates its
    // own (RenderTarget::Enable ()); the back buffer is the bottom of that stack and needs the same,
    // or its draws are recorded nowhere at all.
    CommandList*    m_backBufferList { nullptr };
    // Whether this frame has already drawn on the back buffer. Decides the loadOp of the NEXT scope
    // on it: the first one in a frame throws the old content away, every later one loads it, or a
    // render target activated in between would cost everything drawn on the screen so far.
    bool            m_backBufferWasWritten { false };

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

    virtual void SetContextAttributes(void) {}

    virtual void SetupDisplay(String windowTitle);

    // Present the current back buffer, advance to the next one.
    virtual void Update(void);

    void EndFrame(void);
    void BeginFrame(void);

    // Applies vertical sync. The present mode belongs to the swapchain, so this rebuilds it - like
    // UpdateDisplayMode () it has to run between two frames, not inside one.
    bool SetVSync(bool vSync);

    inline bool VSync(void) noexcept {
        return m_vSync;
    }

    // Recreates the swapchain for the current dimensions and vertical sync setting.
    bool RecreateSwapchain(void);

    // Transition current back buffer PRESENT/UNDEFINED → COLOR_ATTACHMENT and open a
    // vkCmdBeginRendering scope (loadOp = DONT_CARE; clears go through ClearBackBuffer).
    void EnableBackBuffer(void) noexcept;

    // Close the vkCmdBeginRendering scope and transition COLOR_ATTACHMENT → PRESENT_SRC_KHR.
    // Call before Present.
    void DisableBackBuffer(void) noexcept;

    // Close the scope and NOTHING else - the image stays a colour attachment. What a render target
    // becoming the draw buffer needs: Vulkan allows no second rendering scope inside an open one,
    // and the back buffer keeps its content for the scope that follows.
    void SuspendBackBuffer(void) noexcept;

    // The list everything on the back buffer is recorded into: the current one when there is one,
    // otherwise the back buffer's own, opened here. Null only when even that fails.
    CommandList* BackBufferList(void) noexcept;

    inline bool IsInRendering(void) const noexcept { return m_isInRendering; }

    inline VkImage CurrentBackBuffer(void) const noexcept {
        return m_swapchain.Image(m_backBufferIndex);
    }

    inline VkImageView CurrentBackBufferView(void) const noexcept {
        return m_swapchain.ImageView(m_backBufferIndex);
    }

    inline ImageLayoutTracker& CurrentBackBufferTracker(void) noexcept {
        return m_swapchain.LayoutTracker(m_backBufferIndex);
    }

    // The swap chain's pixel format - what a readback of the back buffer (GfxRenderer::ReadBuffer ())
    // has to swizzle from.
    inline VkFormat CurrentBackBufferFormat(void) const noexcept {
        return m_swapchain.Format();
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
