
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

void MovingFrameCounter::Update(void) {
    size_t t = SDL_GetTicks();
    if (m_renderStartTime > 0) {
        float dt = float(t - m_renderStartTime) * 0.001f;
        if ((dt < 1.0f) and (dt > 0.0f)) {
            m_movingTotalTime -= m_movingFrameTimes[m_movingFrameIndex] - dt;
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
