#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "glew.h"
//#include "quad.h"
#include "gfxdriverstates.h"
#include "drawbufferhandler.h"

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view transformation
// the renderer enforces window width >= window height, so for portrait screen mode, the window contents
// rendered sideways. That's why DrawBufferHandler class has m_windowWidth, m_windowHeight and m_aspectRatio
// separate from DisplayHandler.

void DrawBufferHandler::SetActiveDrawBuffers(void) {
    glDrawBuffers(ActiveDrawBuffers()->Length(), ActiveDrawBuffers()->Data());
    if (m_drawBufferInfo.m_renderTarget)
        glViewport(0, 0, m_drawBufferInfo.m_renderTarget->GetWidth(true), m_drawBufferInfo.m_renderTarget->GetHeight(true));
    else
        glViewport(0, 0, m_windowWidth, m_windowHeight);
}


bool DrawBufferHandler::SetActiveBuffer(RenderTarget* buffer, bool clearBuffer) {
    if (m_activeBuffer != buffer) {
        if (m_activeBuffer)
            m_activeBuffer->Disable();
        m_activeBuffer = buffer;
    }
    return m_activeBuffer and m_activeBuffer->Reenable(clearBuffer);
}


void DrawBufferHandler::SetupDrawBuffers(void) {
    m_defaultDrawBuffers.Resize(1);
    m_defaultDrawBuffers[0] = GL_BACK;
    m_drawBufferInfo.Update(nullptr, &m_defaultDrawBuffers);
    ResetDrawBuffers(nullptr); // required to initialize m_drawBufferInfo. If not done here, subsequent renders to render targets ahead of main rendering loop will crash the app
}


void DrawBufferHandler::ResetDrawBuffers(RenderTarget* activeBuffer, bool clearBuffer) {
    // m_defaultDrawBufferInfo must always be the first entry in the drawBufferStack, so it must be the final draw buffer info retrieved
    while ((m_drawBufferStack.Length() > 0) and m_drawBufferStack.Pop(m_drawBufferInfo)) {
        if (m_drawBufferInfo.m_renderTarget)
            m_drawBufferInfo.m_renderTarget->Disable();
    }
    //m_drawBufferInfo.Update(nullptr, &m_defaultDrawBuffers);
    if (not SetActiveBuffer(activeBuffer, clearBuffer)) {
        SetActiveDrawBuffers();
    }
}


void DrawBufferHandler::SaveDrawBuffer() {
    m_drawBufferStack.Push(m_drawBufferInfo);
}


void DrawBufferHandler::SetDrawBuffers(RenderTarget* renderTarget, AutoArray<GLuint>* drawBuffers) {
    if ((renderTarget == nullptr) or (m_drawBufferInfo.m_renderTarget == nullptr) or (renderTarget->m_handle != m_drawBufferInfo.m_renderTarget->m_handle)) {
        SaveDrawBuffer();
        m_drawBufferInfo = DrawBufferInfo(renderTarget, drawBuffers);
    }
    SetActiveDrawBuffers();
}


void DrawBufferHandler::RestoreDrawBuffer(void) {
    m_drawBufferStack.Pop(m_drawBufferInfo);
    gfxDriverStates.BindTexture2D(0, 0);
    if (m_drawBufferInfo.m_renderTarget != nullptr)
        m_drawBufferInfo.m_renderTarget->Reenable(false, true);
    else
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SetActiveDrawBuffers();
}


void DrawBufferHandler::RemoveDrawBuffer(RenderTarget* buffer) {
    for (auto& dbi : m_drawBufferStack)
        if (dbi.m_renderTarget == buffer)
            m_drawBufferStack.Remove(dbi);
}

// =================================================================================================
