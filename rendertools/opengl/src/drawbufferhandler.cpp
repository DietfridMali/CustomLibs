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
#endif

// =================================================================================================

void DrawBufferHandler::Setup(int windowWidth, int windowHeight) {
    m_parentBuffer = nullptr;
    m_activeBuffer = nullptr;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
    m_defaultDrawBuffers.Resize(1);
    m_defaultDrawBuffers[0] = GL_BACK;
}


void DrawBufferHandler::SetActiveDrawBuffers(void) {
    DrawBufferList& drawBuffers = m_activeBuffer ? m_activeBuffer->DrawBuffers() : m_defaultDrawBuffers;
#if 0
    if (m_activeBuffer)
        glViewport(0, 0, m_activeBuffer->GetWidth(true), m_activeBuffer->GetHeight(true));
    else
        glViewport(0, 0, m_windowWidth, m_windowHeight);
#endif
    if (not drawBuffers.IsEmpty())
        glDrawBuffers(drawBuffers.Length(), drawBuffers.Data());
}


void DrawBufferHandler::ActivateDrawBuffer(RenderTarget* buffer) {
    if (buffer != m_activeBuffer) {
        if (m_activeBuffer) {
            m_parentBuffer = m_activeBuffer;
            m_drawBufferStack.Push(m_activeBuffer);
        }
        m_activeBuffer = buffer;
        SetActiveDrawBuffers();
    }
}


bool DrawBufferHandler::DeactivateDrawBuffer(RenderTarget* buffer) {
    if (buffer != m_activeBuffer)
        return false;
    if (m_drawBufferStack.IsEmpty())
        m_activeBuffer = nullptr;
    else {
        m_activeBuffer = m_drawBufferStack.Pop();
        m_parentBuffer = m_drawBufferStack.IsEmpty() ? nullptr : m_drawBufferStack.Last();
        m_activeBuffer->Reenable();
    }
    SetActiveDrawBuffers();
    return true;
}


void DrawBufferHandler::ResetDrawBuffers(void) {
    while (m_activeBuffer)
        m_activeBuffer->Disable();
}

// =================================================================================================
