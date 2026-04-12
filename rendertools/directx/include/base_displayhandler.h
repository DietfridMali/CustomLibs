#pragma once

#include <math.h>
#include <stdlib.h>
#include <functional>

#include "framework.h"
#include "std_defines.h"
#include "string.hpp"
#include "basesingleton.hpp"
#include "sdlhandler.h"
#include "descriptor_heap.h"

// =================================================================================================
// DX12 DisplayHandler: manages the SDL window, extracts the Win32 HWND, creates the DXGI swap chain
// with double-buffered back buffers, and provides the RTV descriptors for each back buffer.
//
// Startup order (caller's responsibility):
//   1. DX12Context::Create()
//   2. CommandQueueHandler::Create(device)
//   3. DescriptorHeapHandler::Create(device)
//   4. BaseDisplayHandler::Create()

class BaseDisplayHandler
    : public PolymorphSingleton<BaseDisplayHandler>
{
public:
    static constexpr UINT BACK_BUFFER_COUNT = 2;
    static constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

    int             m_width;
    int             m_height;
    int             m_maxWidth;
    int             m_maxHeight;
    bool            m_isFullscreen;
    bool            m_vSync;
    bool            m_isLandscape;
    float           m_aspectRatio;

    SDL_Window*                     m_window;
    HWND                            m_hwnd;
    ComPtr<IDXGISwapChain3>         m_swapChain;
    ComPtr<ID3D12Resource>          m_backBuffers[BACK_BUFFER_COUNT];
    DescriptorHandle                m_rtvHandles[BACK_BUFFER_COUNT];
    UINT                            m_backBufferIndex{ 0 };

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
        , m_hwnd(nullptr)
        , m_backBufferIndex(0)
        , m_activeDisplayMode(0)
    {
        _instance = this;
    }

    virtual ~BaseDisplayHandler();

    bool Init(void);

    int GetDisplayModes(void);

    void Create(String windowTitle = "", int width = 1920, int height = 1080,
                bool useFullscreen = true, bool vSync = false);

    static BaseDisplayHandler& Instance(void) {
        return dynamic_cast<BaseDisplayHandler&>(PolymorphSingleton::Instance());
    }

    int FindDisplayMode(int width, int height);

    virtual void ComputeDimensions(int width, int height, bool useFullscreen) noexcept;

    virtual void SetupDisplay(String windowTitle);

    // Present the current back buffer, advance to the next one.
    virtual void Update(void);

    // Returns the current back buffer resource (set as render target before drawing).
    inline ID3D12Resource* CurrentBackBuffer(void) const noexcept {
        return m_backBuffers[m_backBufferIndex].Get();
    }

    // Returns the CPU-side RTV handle for the current back buffer.
    inline D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV(void) const noexcept {
        return m_rtvHandles[m_backBufferIndex].cpu;
    }

    inline int    GetWidth(void)       noexcept { return m_width; }
    inline int    GetHeight(void)      noexcept { return m_height; }
    inline float  GetAspectRatio(void) noexcept { return m_aspectRatio; }
    inline SDL_Window*    GetWindow(void)   noexcept { return m_window; }
    inline HWND           GetHwnd(void)     noexcept { return m_hwnd; }
    inline IDXGISwapChain3* SwapChain(void) noexcept { return m_swapChain.Get(); }

    inline const AutoArray<SDL_DisplayMode>& DisplayModes(void) const noexcept {
        return m_displayModes;
    }

    inline const SDL_DisplayMode& GetDisplayMode(int i = -1) const noexcept {
        return m_displayModes[((i < 0) || (i >= m_displayModes.Length())) ? m_activeDisplayMode : i];
    }

    inline int  SelectedDisplayMode(void) noexcept { return m_activeDisplayMode; }
    inline void SelectDisplayMode(int displayMode) noexcept { m_activeDisplayMode = displayMode; }

    bool ChangeDisplayMode(int displayMode, bool useFullscreen);

    inline bool IsFullScreen(void)               noexcept { return m_isFullscreen; }
    inline void SetFullScreen(bool useFullscreen) noexcept { m_isFullscreen = useFullscreen; }

    bool SwitchDisplayMode(int direction);

    bool ToggleFullscreen(void);

    inline bool DisplayModeHasChanged(int& lastDisplayMode) noexcept {
        if (lastDisplayMode == m_activeDisplayMode) return false;
        lastDisplayMode = m_activeDisplayMode;
        return true;
    }

    virtual void OnResize(void) {}

private:
    // Acquires back buffer resources from the swap chain and creates RTVs.
    bool AcquireBackBuffers(void) noexcept;

    // Releases back buffer references (required before ResizeBuffers).
    void ReleaseBackBuffers(void) noexcept;
};

#define baseDisplayHandler BaseDisplayHandler::Instance()

// =================================================================================================
