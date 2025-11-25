
#include "std_defines.h"
#include "glew.h"
#include "SDL.h"
#include "base_displayhandler.h"
#include <cstdint>
#include <limits>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
#include <stdexcept>

// =================================================================================================

bool BaseDisplayHandler::Init(void) {
    if (sdlHandler.Init(SDL_INIT_VIDEO) != 0)
        return false;
    return GetDisplayModes() > 0;
}


int BaseDisplayHandler::GetDisplayModes(void) {
    int n = SDL_GetNumDisplayModes(0);
    ManagedArray<SDL_DisplayMode> m(n);
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

// find display mode closest to width*height with aspect ratio width/height
// aspect ratio comes first
int BaseDisplayHandler::FindDisplayMode(int width, int height) {
    float aspectRatio = float(width) / float(height);
    int64_t size = int64_t(width) * int64_t(height);
    int bestMode = -1;
    float daMin = 1e6;
    int64_t dsMin = std::numeric_limits<int64_t>::max();
    for (int i = 0; i < m_displayModes.Length(); ++i) {
        SDL_DisplayMode& m = m_displayModes[i];
        float da = float(m.w) / float(m.h) - aspectRatio;
        int64_t ds = static_cast<int64_t>(std::llabs((int64_t(m.w) * int64_t(m.h)) - size));
        if (da < daMin) {
            daMin = da;
            bestMode = i;
        }
        else if ((da == daMin) and (ds < dsMin)) {
            ds = dsMin;
            bestMode = i;
        }
    }
    return bestMode;
}


void BaseDisplayHandler::Create(String windowTitle, int width, int height, bool fullscreen, bool vSync) {
    m_activeDisplayMode = FindDisplayMode(width, height);
    width = m_displayModes[m_activeDisplayMode].w;
    height = m_displayModes[m_activeDisplayMode].h;
    SDL_Rect rect;
    SDL_GetDisplayBounds(0, &rect);
    m_maxWidth = rect.w;
    m_maxHeight = rect.h;
    ComputeDimensions(width, height, fullscreen);
    m_aspectRatio = float(m_width) / float(m_height);
    m_isLandscape = m_width > m_height;
    m_vSync = vSync;
    SetupDisplay(windowTitle);
}


void BaseDisplayHandler::ComputeDimensions(int width, int height, bool fullscreen) noexcept {
    if (width * height == 0) {
        m_width = m_maxWidth;
        m_height = m_maxHeight;
        m_fullscreen = true;
    }
    else {
        m_width = std::min(width, m_maxWidth);
        m_height = std::min(height, m_maxHeight);
        m_fullscreen = fullscreen;
    }
}


BaseDisplayHandler::~BaseDisplayHandler() {
    if (m_context != SDL_GLContext(0))
        SDL_GL_DeleteContext(m_context);
}


void BaseDisplayHandler::SetupDisplay(String windowTitle) {
    int screenType = SDL_WINDOW_OPENGL;
#if 0
    if (m_fullscreen) {
        if ((m_width != m_maxWidth) or (m_height != m_maxHeight))
            screenType |= SDL_WINDOW_BORDERLESS;
        else
            screenType |= SDL_WINDOW_FULLSCREEN; // don't use SDL_WINDOW_FULLSCREEN_DESKTOP, as it can cause problems on scaled Linux desktops
        m_fullscreen = true;
    }
#else
    if (m_fullscreen)
        screenType |= SDL_WINDOW_FULLSCREEN; // don't use SDL_WINDOW_FULLSCREEN_DESKTOP, as it can cause problems on scaled Linux desktops
#endif
#if 1
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#endif
    try {
        m_window = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_width, m_height, screenType);
    }
    catch (...) {
        m_window = nullptr;
    }
    if (not m_window) {
        fprintf(stderr, "Smiley-Battle: Couldn't set screen mode (%d x %d) (error '%s')\n", m_width, m_height, SDL_GetError());
        exit(1);
    }
    try {
        m_context = SDL_GL_CreateContext(m_window);
    }
    catch (...) {
        m_context = nullptr;
    }
    if (not m_context) {
        fprintf(stderr, "Smiley-Battle: Couldn't get OpenGL context (error '%s')\n", SDL_GetError());
        exit(1);
    }
    SDL_GL_SetSwapInterval(m_vSync ? 1 : 0);
}


void BaseDisplayHandler::Update(void) {
    SDL_GL_SwapWindow(m_window);
}


bool BaseDisplayHandler::ChangeDisplayMode(int displayMode, bool fullscreen) {
    if (displayMode >= m_displayModes.Length())
        return false;

    if (displayMode < 0)
        displayMode = m_activeDisplayMode;

    if (m_activeDisplayMode != displayMode) {
        m_activeDisplayMode = displayMode;
        m_fullScreen = fullscreen;
        SDL_DisplayMode mode = GetDisplayMode();
        if (m_fullScreen) {
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
    }
    else if (m_fullScreen != fullscreen) {
        m_fullScreen = fullscreen;
        SDL_SetWindowFullscreen(m_window, m_fullScreen ? SDL_WINDOW_FULLSCREEN : 0);
    }
    return true;
}


bool BaseDisplayHandler::SwitchDisplayMode(int direction) {
    return ChangeDisplayMode(m_activeDisplayMode + direction, m_fullScreen);
}


bool BaseDisplayHandler::ToggleFullscreen(void) {
    return ChangeDisplayMode(-1, not m_fullscreen);
}


BaseDisplayHandler* baseDisplayHandlerInstance = nullptr;

// =================================================================================================
