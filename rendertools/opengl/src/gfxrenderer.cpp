#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "glew.h"
#include "conversions.hpp"
#include "tristate.h"
#include "gfxrenderer.h"
#include "shadowmap.h"
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
    

void GfxRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    if (m_screenIsAvailable) {
        m_frameCounter.Draw(true);
        Stop2DScene();
        m_screenIsAvailable = false;
        if (m_screenBuffer) {
            gfxStates.DepthFunc(GfxOperations::CompareFunc::Always);
            gfxStates.SetFaceCulling(0); // required for vertical flipping because that inverts the buffer's winding
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

#pragma warning(pop)

// =================================================================================================
