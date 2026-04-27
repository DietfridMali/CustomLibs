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
}


void DrawBufferHandler::SetActiveDrawBuffers(void) {
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
		m_activeBuffer->Disable(); // close command list
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
