#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "glew.h"
#include "conversions.hpp"
#include "tristate.h"
#include "gfxrenderer.h"
#include "shadowmap.h"
#include "tracy_wrapper.h"
#include <cstdint>                 // TracyOpenGL.hpp's USE_TRACY=0 no-op branch uses int32_t without pulling <cstdint>
#include "base_shaderhandler.h"
#include "base_displayhandler.h"
#include "gfxrenderer.h"
#include "gfxapitype.h"
#include "gfxrenderer.h"

#ifdef _DEBUG
static Texture* testTexture = nullptr;
#endif

#define LOG_OPERATIONS 0

// =================================================================================================
// DX12 Renderer

bool GfxRenderer::InitGraphics(void) {
    GLint i = glewInit();
    if (i != GLEW_OK) {
        fprintf(stderr, "Smiley-Battle: Cannot initialize GLEW.\n");
        return false;
    }
    glGetIntegerv(GL_MAJOR_VERSION, &m_glVersion.major);
    glGetIntegerv(GL_MINOR_VERSION, &m_glVersion.minor);
#if USE_TRACY
    TracyGpuContext;
#endif
    return true;
}

#pragma warning(push)
#pragma warning(disable:4100)
void* GfxRenderer::StartOperation(String name, bool piggyback) noexcept {
    return nullptr;
}


bool GfxRenderer::FinishOperation(void* cl, bool flush) noexcept {
    return true;
}

#pragma warning(pop)


void GfxRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    ZoneScoped;
    TracyGpuZone("DrawScreen");
    if (m_screenIsAvailable) {
        m_frameCounter.Draw(true);
        Stop2DScene();
        UpdateFrameMetrics();
        if (m_screenBuffer) {
            Set2DRenderStates();
            //SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));
#if 0
            if (m_screenBuffer->Activate({})) {
                RenderToViewport(testTexture, ColorData::White, false, false);
                m_screenBuffer->Deactivate();
            }
#endif
            gfxStates.ClearColorBuffers();
            RenderToViewport(m_screenBuffer->GetAsTexture({}), ColorData::White, bRotate, bFlipVertically);
        }
    }
}

// =================================================================================================
// The window's back buffer as DrawScreen () left it - so call this BEFORE the buffer swap; after it the
// picture has moved to the front buffer, which not every driver hands back. Bottom row first is what
// glReadPixels delivers anyway; DX and VK turn their top down copies around to match.

bool GfxRenderer::ReadBuffer(void* buffer, size_t bufferSize, int x, int y, int width, int height) {
    if (not buffer)
        return false;

    int w = WindowWidth();
    int h = WindowHeight();

    if (width <= 0)
        width = w - x;
    if (height <= 0)
        height = h - y;
    if ((x < 0) or (y < 0) or (width <= 0) or (height <= 0) or (x + width > w) or (y + height > h))
        return false;
    if (bufferSize < size_t(width) * size_t(height) * 4)
        return false;

    GLint prevFramebuffer = 0;
    GLint prevReadBuffer = 0;
    GLint prevAlignment = 4;

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);   // packed rows, whatever the width
    gfxStates.ClearError();
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer);

    bool ok = gfxStates.CheckError("ReadBuffer");

    glPixelStorei(GL_PACK_ALIGNMENT, prevAlignment);
    glReadBuffer(GLenum(prevReadBuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(prevFramebuffer));
    return ok;
}

// =================================================================================================
