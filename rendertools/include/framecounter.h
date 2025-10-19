
#pragma once

#include "timer.h"
#include "viewport.h"
#include "colordata.h"

// =================================================================================================

class BaseFrameCounter {
protected:
    RGBAColor               m_color;
    Viewport                m_viewport;
    uint64_t                m_renderStartTime{ 0 };
    Timer                   m_drawTimer{ 1000 };
    float                   m_fps{ 0.0f };
    bool                    m_showFps{ true };

public:
    BaseFrameCounter()
        : m_color(ColorData::White), m_viewport({ 0, 0, 0, 0 }), m_fps(0.0f), m_renderStartTime(0), m_showFps(true)
    {
        Reset();
    }

    virtual ~BaseFrameCounter() = default;

    inline void Setup(Viewport viewport, RGBAColor color = ColorData::White) noexcept {
        m_viewport = viewport;
        m_color = color;
    }

    virtual void Draw(bool update);

    virtual bool Start(void) {
        if (m_renderStartTime != 0)
            return false;
        m_renderStartTime = SDL_GetPerformanceCounter();
        m_drawTimer.Start();
        return true;
    }

    inline void ShowFps(bool showFps) noexcept {
        m_showFps = showFps; 
    }

    inline bool IsActive(void) noexcept {
        return m_showFps;
    }

    void Toggle(void) noexcept {
        m_showFps = not m_showFps;
        if (m_showFps) {
            Reset();
            Start();
        }
    }

    virtual void Reset(void) {
        m_renderStartTime = 0;
    }

    virtual void Update(void) = 0;

    virtual float GetFps(void) const = 0;
};

// -------------------------------------------------------------------------------------------------

constexpr size_t        FrameWindowSize = 90;

class MovingFrameCounter 
    : public BaseFrameCounter
{
private:
    SimpleArray<uint64_t, FrameWindowSize>  m_movingFrameTimes{};
    uint64_t                                m_movingTotalTicks{ 0 };
    int                                     m_movingFrameIndex{ 0 };
    int                                     m_movingFrameCount{ 0 };
    uint64_t                                m_frequency{ 0 };

public:
    MovingFrameCounter() = default;

    virtual ~MovingFrameCounter() = default;

    virtual bool Start(void);

    virtual void Reset(void);

    virtual void Update(void);

    virtual float GetFps(void) const;
};

// -------------------------------------------------------------------------------------------------

class LinearFrameCounter
    : public BaseFrameCounter
{
public:
    size_t                  m_frameCount[2]; // total frame count / frames during last second
    float                   m_fps[2];
    size_t                  m_renderStartTime;
    Timer                   m_fpsTimer{ 1000 };

    LinearFrameCounter() = default;

    virtual ~LinearFrameCounter() = default;

    virtual bool Start(void) {
        if (not BaseFrameCounter::Start())
            return false;
        m_fpsTimer.Start();
        return true;
    }

    virtual void Reset(void) {
        BaseFrameCounter::Reset();
        m_frameCount[0] = m_frameCount[1] = 0;
        m_fps[0] = m_fps[1] = 0;
        m_renderStartTime = 0;
    }

    virtual void Update(void);

    virtual float GetFps(int i = -1) const {
        return m_fps[i];
    }
};

// =================================================================================================
