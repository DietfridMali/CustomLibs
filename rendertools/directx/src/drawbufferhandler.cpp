#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "gfxstates.h"
#include "drawbufferhandler.h"
#include "rendertarget.h"

#ifdef OPENGL
#include "glew.h"
#include "conversions.hpp"
#else
#include "base_displayhandler.h"
#include "base_renderer.h"
#endif

// =================================================================================================

void DrawBufferHandler::Setup(int windowWidth, int windowHeight) {
    m_parentBuffer = nullptr;
    m_activeBuffer = nullptr;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
}


void DrawBufferHandler::SetActiveDrawBuffers(void) {
}


void DrawBufferHandler::ActivateDrawBuffer(RenderTarget* buffer) {
    if (buffer != m_activeBuffer) {
        if (m_activeBuffer) {
            m_parentBuffer = m_activeBuffer;
            // m_activeBuffer->Disable() restores the render states of the RT that was active before m_activeBuffer.
            // However, we are tracking render states globally, and the code activating the new RT depends on these.
            // So the current render target's render states need to be saved and restored after Disable() for the new active RT.
            RenderStates rs = baseRenderer.RenderStates();
            m_activeBuffer->Disable(); 
            baseRenderer.RenderStates() = rs;
            m_drawBufferStack.Push(m_activeBuffer);
        }
        m_activeBuffer = buffer;
        SetActiveDrawBuffers();
    }
}


bool DrawBufferHandler::DeactivateDrawBuffer(RenderTarget* buffer) {
    if (buffer != m_activeBuffer)
        return false;
    m_activeBuffer->Disable(); // close command list
    if (m_drawBufferStack.IsEmpty())
        m_activeBuffer = nullptr;
    else {
        m_activeBuffer = m_drawBufferStack.Pop();
        m_parentBuffer = m_drawBufferStack.IsEmpty() ? nullptr : m_drawBufferStack.Last();
        m_activeBuffer->Reactivate();
    }
    SetActiveDrawBuffers();
    return true;
}


void DrawBufferHandler::ResetDrawBuffers(void) {
    while (m_activeBuffer)
        m_activeBuffer->Deactivate();
}

// =================================================================================================
