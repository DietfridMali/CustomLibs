#include "std_defines.h"
#include "base_displayhandler.h"
#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "commandlist.h"
#include "vkcontext.h"
#include "descriptor_pool_handler.h"
#include "cbv_allocator.h"
#include "resource_handler.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL.h"
#include "SDL_vulkan.h"
#pragma warning(pop)

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// =================================================================================================
// Vulkan BaseDisplayHandler

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
    width = m_displayModes[m_activeDisplayMode].w;
    height = m_displayModes[m_activeDisplayMode].h;
    SDL_Rect rect;
    SDL_GetDisplayBounds(0, &rect);
    m_maxWidth = rect.w;
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
        m_width = std::min(width, m_maxWidth);
        m_height = std::min(height, m_maxHeight);
        m_isFullscreen = useFullscreen;
    }
}


void BaseDisplayHandler::SetupDisplay(String windowTitle) {
    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN;
    if (m_isFullscreen)
        windowFlags |= SDL_WINDOW_FULLSCREEN;

    m_window = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_width, m_height, windowFlags);
    if (not m_window) {
        fprintf(stderr, "BaseDisplayHandler: SDL_CreateWindow failed (%s)\n", SDL_GetError());
        exit(1);
    }
    // Swapchain creation is deferred to SetupSwapchain (called from gfxRenderer::InitGraphics
    // after VKContext + Device + Surface are up).
}


bool BaseDisplayHandler::SetupSwapchain(void) {
    VkSurfaceKHR surface = vkContext.Surface();
    if (surface == VK_NULL_HANDLE) {
        fprintf(stderr, "BaseDisplayHandler::SetupSwapchain: VKContext has no surface\n");
        return false;
    }
    if (not m_swapchain.Create(surface, uint32_t(m_width), uint32_t(m_height), m_vSync)) {
        fprintf(stderr, "BaseDisplayHandler::SetupSwapchain: Swapchain::Create failed\n");
        return false;
    }
    if (not commandListHandler.CmdQueue().InitSyncObjects(m_swapchain.Handle())) {
        fprintf(stderr, "BaseDisplayHandler::SetupSwapchain: InitSyncObjects failed\n");
        return false;
    }
    m_backBufferIndex = 0;
    return true;
}


// EnableBackBuffer / DisableBackBuffer drive the swapchain image's layout transitions for the
// presentable color attachment AND the matching vkCmdBeginRendering / vkCmdEndRendering scope.
// Both run on the currently active CommandList's command buffer (top of the CommandListHandler
// stack).
//
// EnableBackBuffer is called at the start of each frame's main render pass to bring the
// acquired swapchain image from PRESENT_SRC_KHR (or UNDEFINED on first use) to
// COLOR_ATTACHMENT_OPTIMAL and open a dynamic-rendering scope with loadOp = DONT_CARE — the
// common path overwrites the back buffer with the screen-buffer blit, so a load is wasted.
// Callers that need a real clear (e.g. the screen-buffer-less fallback in Start2DScene) issue
// gfxStates.ClearBackBuffer() inside the scope; that path uses vkCmdClearAttachments.
//
// DisableBackBuffer closes the scope and transitions back to PRESENT_SRC_KHR before
// vkQueuePresentKHR.

void BaseDisplayHandler::EnableBackBuffer(void) noexcept {
    VkCommandBuffer cb = commandListHandler.CmdQueue().CmdBuffer();
    if (cb == VK_NULL_HANDLE)
        return;
    m_swapchain.LayoutTracker(m_backBufferIndex).ToColorAttachment(cb);

    if (m_isInRendering)
        return;

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = m_swapchain.ImageView(m_backBufferIndex);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea.offset = { 0, 0 };
    info.renderArea.extent = m_swapchain.Extent();
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;

    vkCmdBeginRendering(cb, &info);
    m_isInRendering = true;
}


void BaseDisplayHandler::DisableBackBuffer(void) noexcept {
    VkCommandBuffer cb = commandListHandler.CmdQueue().CmdBuffer();
    if (cb == VK_NULL_HANDLE)
        return;
    if (m_isInRendering) {
        vkCmdEndRendering(cb);
        m_isInRendering = false;
    }
    m_swapchain.LayoutTracker(m_backBufferIndex).ToPresent(cb);
}


void BaseDisplayHandler::EndFrame(void) {
    if (m_swapchain.Handle() == VK_NULL_HANDLE)
        return;
    // Submit all registered command buffers, then present + advance frame slot.
    commandListHandler.ExecuteAll();
    commandListHandler.CmdQueue().EndFrame();
    m_backBufferIndex = commandListHandler.CmdQueue().ImageIndex();
}


void BaseDisplayHandler::BeginFrame(void) {
    if (m_swapchain.Handle() == VK_NULL_HANDLE)
        return;
    // CmdQueue::BeginFrame waits for the slot's in-flight fence, vkResetFences, and
    // vkAcquireNextImageKHR. We follow up with the per-frame resource resets that the DX12
    // path runs implicitly inside CommandQueue::BeginFrame.
    commandListHandler.CmdQueue().BeginFrame();
    m_backBufferIndex = commandListHandler.CmdQueue().ImageIndex();
    const uint32_t slot = commandListHandler.CmdQueue().FrameIndex();
    descriptorPoolHandler.BeginFrame(slot);
    cbvAllocator.Reset(slot);
    gfxResourceHandler.Cleanup(slot);
    commandListHandler.ResetBindings();
    baseShaderHandler.InvalidateActiveShader();
}


void BaseDisplayHandler::Update(void) {
    EndFrame();
    BeginFrame();
}


BaseDisplayHandler::~BaseDisplayHandler() {
    m_swapchain.Destroy();
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}


bool BaseDisplayHandler::UpdateDisplayMode(int displayMode, bool useFullscreen) {
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
    m_width = mode.w;
    m_height = mode.h;
    m_aspectRatio = float(m_width) / float(m_height);

    // Resize the swapchain on dimension change.
    if (m_swapchain.Handle() != VK_NULL_HANDLE) {
        VkSurfaceKHR surface = vkContext.Surface();
        if (not m_swapchain.Recreate(surface, uint32_t(m_width), uint32_t(m_height), m_vSync)) {
            fprintf(stderr, "BaseDisplayHandler::UpdateDisplayMode: Swapchain::Recreate failed\n");
            return false;
        }
        m_backBufferIndex = 0;
        // The semaphores in CommandQueue are still valid (per-slot, swapchain-independent),
        // but the cached m_swapchain handle inside CommandQueue is stale — refresh it.
        commandListHandler.CmdQueue().m_swapchain = m_swapchain.Handle();
    }
    return true;
}


void BaseDisplayHandler::SwitchDisplayMode(int direction) {
    RequestDisplayChange(m_activeDisplayMode + direction, m_isFullscreen);
}


void BaseDisplayHandler::ToggleFullscreen(void) {
#ifdef _DEBUG
    fprintf(stderr, "Toggle fullscreen -> %d\n", m_isFullscreen ? 0 : 1);
#endif
    RequestDisplayChange(-1, !m_isFullscreen);
}

// =================================================================================================
