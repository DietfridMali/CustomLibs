
#pragma once

#include "timer.h"
#include "viewport.h"
#include "colordata.h"

// =================================================================================================

class BaseFrameCounter {
protected:
    RGBAColor               m_color;
    Viewport                m_viewport;
    size_t                  m_renderStartTime{ 0 };
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

    void Setup(Viewport viewport, RGBAColor color = ColorData::White) {
        m_viewport = viewport;
        m_color = color;
    }

    virtual void Draw(bool update);

    virtual bool Start(void) {
        if (m_renderStartTime != 0)
            return false;
        m_renderStartTime = SDL_GetTicks();
        m_drawTimer.Start();
        return true;
    }

    void ShowFps(bool showFps) { m_showFps = showFps; }

    void Toggle(void) {
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
    SimpleArray<float, FrameWindowSize> m_movingFrameTimes;
    float                               m_movingTotalTime{ 0.0f };
    int                                 m_movingFrameIndex{ 0 };
    int                                 m_movingFrameCount{ 0 };

public:
    MovingFrameCounter() = default;

    virtual ~MovingFrameCounter() = default;

    virtual void Reset(void) {
        BaseFrameCounter::Reset();
        m_movingFrameTimes.fill(0.0f);
        m_movingTotalTime = 0.0f;
        m_movingFrameIndex = 0;
        m_movingFrameCount = 0;
    }

    virtual void Update(void);

    virtual float GetFps(void) const {
        return (m_movingTotalTime == 0.0f) ? 0.0f : float(m_movingFrameCount / m_movingTotalTime);
    }
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
