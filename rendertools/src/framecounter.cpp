
#include "framecounter.h"
#include "colordata.h"
#include "textrenderer.h"
#include "base_renderer.h"
#include <format>

// =================================================================================================

void BaseFrameCounter::Draw(bool update) {
    if (update)
        Update();
    if (m_drawTimer.HasExpired(0, true))
        m_fps = GetFps();
    if (m_showFps) {
        baseRenderer.SetViewport(m_viewport, false);
        std::string s = std::format("{:7.2f} fps", m_fps);
        textRenderer.SetColor(m_color);
        textRenderer.Render(s, TextRenderer::taLeft, 0);
    }
}

// -------------------------------------------------------------------------------------------------

float MovingFrameCounter::GetFps(void) const {
    return (m_movingTotalTicks == 0) ? 0.0f : float(double(m_movingFrameCount) * double(m_frequency) / double(m_movingTotalTicks));
}

// -------------------------------------------------------------------------------------------------

bool MovingFrameCounter::Start(void) {
    if (not BaseFrameCounter::Start())
        return false;
    m_frequency = SDL_GetPerformanceFrequency();
    return true;
}
    
// -------------------------------------------------------------------------------------------------

void MovingFrameCounter::Reset(void) {
    BaseFrameCounter::Reset();
    m_movingFrameTimes.fill(0);
    m_movingTotalTicks = 0;
    m_movingFrameIndex = 0;
    m_movingFrameCount = 0;
}

// -------------------------------------------------------------------------------------------------

void MovingFrameCounter::Update(void) {
    uint64_t t = SDL_GetPerformanceCounter();
    if (m_renderStartTime > 0) {
        uint64_t dt = t - m_renderStartTime;
        if ((dt <= 1000000) and (dt > 0)) {
            m_movingTotalTicks -= m_movingFrameTimes[m_movingFrameIndex]; // remove oldest frame time from total
            m_movingTotalTicks += dt; // add in current frame time
            m_movingFrameTimes[m_movingFrameIndex] = dt;
            m_movingFrameIndex = (m_movingFrameIndex + 1) % FrameWindowSize;
            if (m_movingFrameCount < FrameWindowSize)
                ++m_movingFrameCount;
        }
    }
    m_renderStartTime = t;
}

// -------------------------------------------------------------------------------------------------

void LinearFrameCounter::Update(void) {
    ++m_frameCount[0];
    if (m_fpsTimer.HasExpired(0, true)) {
        m_fps[0] = float(m_frameCount[0]) / float((SDL_GetTicks() - m_renderStartTime) * 0.001f);
        m_fps[1] = float(m_frameCount[1]) / float(m_fpsTimer.LapTime() * 0.001f);
    }
    else
        ++m_frameCount[1];
}

// =================================================================================================
