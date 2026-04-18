#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "gfxdriverstates.h"
#include "drawbufferhandler.h"

#ifdef OPENGL
#include "glew.h"
#include "conversions.hpp"
#else
#include "base_displayhandler.h"
#endif

// =================================================================================================

void DrawBufferHandler::SetActiveDrawBuffers(void) {
#ifdef OPENGL
    glDrawBuffers(ActiveDrawBuffers()->Length(), ActiveDrawBuffers()->Data());
    if (m_drawBufferInfo.m_renderTarget)
        glViewport(0, 0, m_drawBufferInfo.m_renderTarget->GetWidth(true), m_drawBufferInfo.m_renderTarget->GetHeight(true));
    else
        glViewport(0, 0, m_windowWidth, m_windowHeight);
#endif
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
#ifdef OPENGL
    m_defaultDrawBuffers.Resize(1);
    m_defaultDrawBuffers[0] = GL_BACK;
    m_drawBufferInfo.Update(nullptr, &m_defaultDrawBuffers);
#else
    m_drawBufferInfo = DrawBufferInfo(nullptr);
#endif
    ResetDrawBuffers(nullptr);
}


void DrawBufferHandler::ResetDrawBuffers(RenderTarget* activeBuffer, bool clearBuffer) {
    while ((m_drawBufferStack.Length() > 0) and m_drawBufferStack.Pop(m_drawBufferInfo)) {
        if (m_drawBufferInfo.m_renderTarget)
            m_drawBufferInfo.m_renderTarget->Disable();
    }
    if (not SetActiveBuffer(activeBuffer, clearBuffer))
        SetActiveDrawBuffers();
}


void DrawBufferHandler::SaveDrawBuffer() {
    m_drawBufferStack.Push(m_drawBufferInfo);
}


#ifdef OPENGL
void DrawBufferHandler::TrackDrawBuffers(RenderTarget* renderTarget, AutoArray<GLuint>* drawBuffers) {
    if ((renderTarget == nullptr) or (m_drawBufferInfo.m_renderTarget == nullptr) or (renderTarget->m_handle != m_drawBufferInfo.m_renderTarget->m_handle)) {
        SaveDrawBuffer();
        m_drawBufferInfo = DrawBufferInfo(renderTarget, drawBuffers);
    }
    SetActiveDrawBuffers();
}
#else
void DrawBufferHandler::TrackDrawBuffers(RenderTarget* renderTarget) {
    if ((renderTarget == nullptr) || (m_drawBufferInfo.m_renderTarget == nullptr) || (renderTarget != m_drawBufferInfo.m_renderTarget)) {
        SaveDrawBuffer();
        m_drawBufferInfo = DrawBufferInfo(renderTarget);
    }
    SetActiveDrawBuffers();
}
#endif


void DrawBufferHandler::RestoreDrawBuffer(void) {
    m_drawBufferStack.Pop(m_drawBufferInfo);
    gfxDriverStates.BindTexture2D(0, 0);
    if (m_drawBufferInfo.m_renderTarget != nullptr)
        m_drawBufferInfo.m_renderTarget->Reenable(false, true);
#ifdef OPENGL
    else
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
    SetActiveDrawBuffers();
}


void DrawBufferHandler::RemoveDrawBuffer(RenderTarget* buffer) {
    for (auto& dbi : m_drawBufferStack)
        if (dbi.m_renderTarget == buffer)
            m_drawBufferStack.Remove(dbi);
}

// =================================================================================================
