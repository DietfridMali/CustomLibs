#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "glew.h"
//#include "quad.h"
#include "opengl_states.h"
#include "drawbufferhandler.h"

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view transformation
// the renderer enforces window width >= window height, so for portrait screen mode, the window contents
// rendered sideways. That's why DrawBufferHandler class has m_windowWidth, m_windowHeight and m_aspectRatio
// separate from DisplayHandler.

void DrawBufferHandler::SetActiveDrawBuffers(void) {
    glDrawBuffers(ActiveDrawBuffers()->Length(), ActiveDrawBuffers()->Data());
    if (m_drawBufferInfo.m_fbo)
        glViewport(0, 0, m_drawBufferInfo.m_fbo->GetWidth(true), m_drawBufferInfo.m_fbo->GetHeight(true));
    else
        glViewport(0, 0, m_windowWidth, m_windowHeight);
}


bool DrawBufferHandler::SetActiveBuffer(FBO* buffer, bool clearBuffer) {
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
    ResetDrawBuffers(nullptr); // required to initialize m_drawBufferInfo. If not done here, subsequent renders to FBOs ahead of main rendering loop will crash the app
}


void DrawBufferHandler::ResetDrawBuffers(FBO* activeBuffer, bool clearBuffer) {
    // m_defaultDrawBufferInfo must always be the first entry in the drawBufferStack, so it must be the final draw buffer info retrieved
    while ((m_drawBufferStack.Length() > 0) and m_drawBufferStack.Pop(m_drawBufferInfo)) {
        if (m_drawBufferInfo.m_fbo)
            m_drawBufferInfo.m_fbo->Disable();
    }
    //m_drawBufferInfo.Update(nullptr, &m_defaultDrawBuffers);
    if (not SetActiveBuffer(activeBuffer, clearBuffer)) {
        SetActiveDrawBuffers();
    }
}


void DrawBufferHandler::SaveDrawBuffer() {
    m_drawBufferStack.Push(m_drawBufferInfo);
}


void DrawBufferHandler::SetDrawBuffers(FBO* fbo, ManagedArray<GLuint>* drawBuffers) {
    if ((fbo == nullptr) or (m_drawBufferInfo.m_fbo == nullptr) or (fbo->m_handle != m_drawBufferInfo.m_fbo->m_handle)) {
        SaveDrawBuffer();
        m_drawBufferInfo = DrawBufferInfo(fbo, drawBuffers);
    }
    SetActiveDrawBuffers();
}


void DrawBufferHandler::RestoreDrawBuffer(void) {
    m_drawBufferStack.Pop(m_drawBufferInfo);
    openGLStates.BindTexture2D(0, GL_TEXTURE0);
    if (m_drawBufferInfo.m_fbo != nullptr)
        m_drawBufferInfo.m_fbo->Reenable(false, true);
    else
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SetActiveDrawBuffers();
}

// =================================================================================================
