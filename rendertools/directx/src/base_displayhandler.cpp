#include "std_defines.h"
#include "base_displayhandler.h"
#include "base_renderer.h"
#include "base_shaderhandler.h"
#include "commandlist.h"
#include "dx12context.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL.h"
#include "SDL_syswm.h"
#pragma warning(pop)

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// =================================================================================================

bool BaseDisplayHandler::Init(void) {
    if (sdlHandler.Init(SDL_INIT_VIDEO) != 0)
        return false;
    return GetDisplayModes() > 0;
}


int BaseDisplayHandler::GetDisplayModes(void) {
    int n = SDL_GetNumDisplayModes(0);
    AutoArray<SDL_DisplayMode> m(n);
    for (int i = 0; i < n; ++i)
        SDL_GetDisplayMode(0, i, &m[i]);

    std::sort(m_displayModes.begin(), m_displayModes.end(),
        [](const SDL_DisplayMode& a, const SDL_DisplayMode& b) {
            int64_t areaA = int64_t(a.w) * int64_t(a.h);
            int64_t areaB = int64_t(b.w) * int64_t(b.h);
            if (areaA != areaB) 
                return areaA > areaB;
            if (a.w != b.w) 
                return a.w > b.w;
            if (a.refresh_rate != b.refresh_rate) 
                return a.refresh_rate > b.refresh_rate;
            return a.format > b.format;
        });

    int64_t ai = 0, aj = 0;
    for (int i = 0; i < n; ++i) {
        aj = ai;
        ai = int64_t(m[i].h) * int64_t(m[i].w);
        if (ai != aj)
            m_displayModes.Append(m[i]);
    }
    return m_displayModes.Length();
}


int BaseDisplayHandler::FindDisplayMode(int width, int height) {
    float aspectRatio = float(width) / float(height);
    int64_t size = int64_t(width) * int64_t(height);
    int bestMode = -1;
    float daMin = 1e6f;
    int64_t dsMin = std::numeric_limits<int64_t>::max();
    for (int i = 0; i < m_displayModes.Length(); ++i) {
        SDL_DisplayMode& mode = m_displayModes[i];
        float da = fabsf(float(mode.w) / float(mode.h) - aspectRatio);
        int64_t ds = static_cast<int64_t>(std::llabs(int64_t(mode.w) * int64_t(mode.h) - size));
        if (da < daMin) {
            daMin = da;
            dsMin = ds;
            bestMode = i;
        }
        else if (da == daMin && ds < dsMin) {
            dsMin = ds;
            bestMode = i;
        }
    }
    return bestMode;
}


void BaseDisplayHandler::Create(String windowTitle, int width, int height, bool useFullscreen, bool vSync) {
    m_activeDisplayMode = FindDisplayMode(width, height);
    width  = m_displayModes[m_activeDisplayMode].w;
    height = m_displayModes[m_activeDisplayMode].h;
    SDL_Rect rect;
    SDL_GetDisplayBounds(0, &rect);
    m_maxWidth  = rect.w;
    m_maxHeight = rect.h;
    ComputeDimensions(width, height, useFullscreen);
    m_aspectRatio = float(m_width) / float(m_height);
    m_isLandscape = m_width > m_height;
    m_vSync = vSync;
    SetupDisplay(windowTitle);
}


void BaseDisplayHandler::ComputeDimensions(int width, int height, bool useFullscreen) noexcept {
    if (width * height == 0) {
        m_width = m_maxWidth;
        m_height = m_maxHeight;
        m_isFullscreen = true;
    }
    else {
        m_width = std::min(width,  m_maxWidth);
        m_height = std::min(height, m_maxHeight);
        m_isFullscreen = useFullscreen;
    }
}


bool BaseDisplayHandler::CreateSwapChain(void) {
    // ---- Create the DXGI swap chain ----
    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = UINT(m_width);
    scDesc.Height = UINT(m_height);
    scDesc.Format = BACK_BUFFER_FORMAT;
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = BACK_BUFFER_COUNT;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scDesc.Flags = m_vSync ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    ComPtr<IDXGISwapChain1> swapChain1;
    IDXGIFactory4* factory = dx12Context.m_factory.Get();
    ID3D12CommandQueue* queue = commandListHandler.GetQueue();

    HRESULT hr = factory->CreateSwapChainForHwnd(queue, m_hwnd, &scDesc, nullptr, nullptr, &swapChain1);
    if (FAILED(hr)) {
        fprintf(stderr, "BaseDisplayHandler: CreateSwapChainForHwnd failed (hr=0x%08X)\n", (unsigned)hr);
        exit(1);
    }

    // Disable Alt+Enter fullscreen switch (we handle it ourselves via SDL).
    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    hr = swapChain1.As(&m_swapChain);
    if (FAILED(hr)) {
        fprintf(stderr, "BaseDisplayHandler: IDXGISwapChain1 -> IDXGISwapChain3 failed (hr=0x%08X)\n", (unsigned)hr);
        exit(1);
    }

    if (not AcquireBackBuffers()) {
        fprintf(stderr, "BaseDisplayHandler: AcquireBackBuffers failed\n");
        exit(1);
    }
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}


void BaseDisplayHandler::SetupDisplay(String windowTitle) {
    // ---- Create SDL window (no OpenGL flag) ----
    Uint32 windowFlags = SDL_WINDOW_SHOWN;
    if (m_isFullscreen)
        windowFlags |= SDL_WINDOW_FULLSCREEN;

    m_window = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_width, m_height, windowFlags);
    if (not m_window) {
        fprintf(stderr, "BaseDisplayHandler: SDL_CreateWindow failed (%s)\n", SDL_GetError());
        exit(1);
    }

    // ---- Extract the Win32 HWND from the SDL window ----
    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    if (not SDL_GetWindowWMInfo(m_window, &wmInfo)) {
        fprintf(stderr, "BaseDisplayHandler: SDL_GetWindowWMInfo failed (%s)\n", SDL_GetError());
        exit(1);
    }
    m_hwnd = wmInfo.info.win.window;
    if (not CreateSwapChain())
        exit(1);
}


bool BaseDisplayHandler::AcquireBackBuffers(void) noexcept {
    ID3D12Device* device = dx12Context.Device();
    for (UINT i = 0; i < BACK_BUFFER_COUNT; ++i) {
        HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
        if (FAILED(hr)) {
            fprintf(stderr, "BaseDisplayHandler: GetBuffer(%u) failed (hr=0x%08X)\n", i, (unsigned)hr);
            return false;
        }
        // Allocate or reuse RTV descriptor slot
        if (not m_rtvHandles[i].IsValid())
            m_rtvHandles[i] = descriptorHeaps.AllocRTV();
        if (not m_rtvHandles[i].IsValid())
            return false;

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format        = BACK_BUFFER_FORMAT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(m_backBuffers[i].Get(), &rtvDesc, m_rtvHandles[i].cpu);
        m_backBufferStates[i] = D3D12_RESOURCE_STATE_PRESENT;
    }
    return true;
}


void BaseDisplayHandler::EnableBackBuffer(void) noexcept {
    auto* list = commandListHandler.CurrentList();
    if (not list)
        return;
    if (m_backBufferStates[m_backBufferIndex] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        auto* cl = commandListHandler.GetCurrentCmdListObj();
        if (not cl)
            return;
        cl->SetBarrier(m_backBuffers[m_backBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_backBufferStates[m_backBufferIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = CurrentRTV();
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}


void BaseDisplayHandler::DisableBackBuffer(void) noexcept {
    if (m_backBufferStates[m_backBufferIndex] == D3D12_RESOURCE_STATE_PRESENT)
        return;
    auto* cl = commandListHandler.GetCurrentCmdListObj();
    if (not cl)
        return;
    cl->SetBarrier(m_backBuffers[m_backBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_backBufferStates[m_backBufferIndex] = D3D12_RESOURCE_STATE_PRESENT;
}


void BaseDisplayHandler::ReleaseBackBuffers(void) noexcept {
    for (UINT i = 0; i < BACK_BUFFER_COUNT; ++i)
        m_backBuffers[i].Reset();
}


void BaseDisplayHandler::Update(void) {
    if (not m_swapChain) 
        return;
    // Close the main renderer list — registers it for submission.
    // Submit all registered lists (RenderTarget lists first, main list last — registration order).
    commandListHandler.ExecuteAll();
    commandListHandler.CmdQueue().EndFrame();
    UINT syncInterval = m_vSync ? 1 : 0;
    UINT presentFlags = m_vSync ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    HRESULT hr = m_swapChain->Present(syncInterval, presentFlags);
    if (FAILED(hr))
        fprintf(stderr, "BaseDisplayHandler::Update: Present failed (hr=0x%08X)\n", (unsigned)hr);
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    // Wait for the next frame slot to be free, reset CBV allocator.
    // Active-shader tracking is invalidated because BeginFrame resets all DX12 command-list state.
    commandListHandler.CmdQueue().BeginFrame();
    baseShaderHandler.InvalidateActiveShader();
}


BaseDisplayHandler::~BaseDisplayHandler() {
    ReleaseBackBuffers();
    m_swapChain.Reset();
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}


bool BaseDisplayHandler::ChangeDisplayMode(int displayMode, bool useFullscreen) {
    if (displayMode >= m_displayModes.Length())
        return false;
    if (displayMode < 0)
        displayMode = m_activeDisplayMode;

    m_activeDisplayMode = displayMode;
    m_isFullscreen = useFullscreen;
    SDL_DisplayMode mode = GetDisplayMode();

    if (m_isFullscreen) {
        SDL_SetWindowDisplayMode(m_window, &mode);
        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN);
    }
    else {
        SDL_SetWindowFullscreen(m_window, 0);
        SDL_SetWindowSize(m_window, mode.w, mode.h);
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    m_width       = mode.w;
    m_height      = mode.h;
    m_aspectRatio = float(m_width) / float(m_height);

    // Resize the swap chain back buffers
    if (m_swapChain) {
        // GPU must be idle before resize
        commandListHandler.CmdQueue().WaitIdle();
        ReleaseBackBuffers();
        HRESULT hr = m_swapChain->ResizeBuffers(BACK_BUFFER_COUNT,
            UINT(m_width), UINT(m_height), BACK_BUFFER_FORMAT, 0);
        if (FAILED(hr)) {
            fprintf(stderr, "BaseDisplayHandler::ChangeDisplayMode: ResizeBuffers failed (hr=0x%08X)\n",
                    (unsigned)hr);
            return false;
        }
        AcquireBackBuffers();
        m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    }
    OnResize();
    return true;
}


bool BaseDisplayHandler::SwitchDisplayMode(int direction) {
    return ChangeDisplayMode(m_activeDisplayMode + direction, m_isFullscreen);
}


bool BaseDisplayHandler::ToggleFullscreen(void) {
#ifdef _DEBUG
    fprintf(stderr, "Toggle fullscreen -> %d\n", m_isFullscreen ? 0 : 1);
#endif
    return ChangeDisplayMode(-1, !m_isFullscreen);
}

// =================================================================================================
