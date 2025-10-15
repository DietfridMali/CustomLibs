#pragma once

#include "std_defines.h"
#include <stdlib.h>
#include <math.h>
#include <functional>
#include "string.hpp"
#include "basesingleton.hpp"

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view matrix

class BaseDisplayHandler 
    : public PolymorphSingleton<BaseDisplayHandler>
{
public:
    int             m_width;
    int             m_height;
    int             m_maxWidth;
    int             m_maxHeight;
    bool            m_fullscreen;
    bool            m_vSync;
    bool            m_isLandscape;
    float           m_aspectRatio;
    SDL_Window*     m_window;
    SDL_GLContext   m_context;

    BaseDisplayHandler()
        : m_width(0)
        , m_height(0)
        , m_maxWidth(0)
        , m_maxHeight(0)
        , m_fullscreen(false)
        , m_vSync(true)
        , m_isLandscape(false)
        , m_aspectRatio(1.0f)
        , m_window(nullptr)
        , m_context(SDL_GLContext(0))
    { 
        _instance = this;
    }

    virtual ~BaseDisplayHandler();

    void Create (String windowTitle = "", int width = 1920, int height = 1080, bool fullscreen = true, bool vSync = false);

    static BaseDisplayHandler& Instance(void) { return dynamic_cast<BaseDisplayHandler&>(PolymorphSingleton::Instance()); }

    virtual void ComputeDimensions(int width, int height, bool fullscreen)
        noexcept;

    virtual void SetupDisplay(String windowTitle);

    virtual void Update(void);

    inline int GetWidth(void) noexcept {
        return m_width;
    }

    inline int GetHeight(void) noexcept {
        return m_height;
    }

    inline float GetAspectRatio(void) noexcept {
        return m_aspectRatio;
    }

    inline SDL_Window* GetWindow(void) noexcept {
        return m_window;
    }

    inline SDL_GLContext GetContext(void) noexcept {
        return m_context;
    }

};

#define baseDisplayHandler BaseDisplayHandler::Instance()

// =================================================================================================

