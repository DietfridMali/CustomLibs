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
    m_activeBuffer = nullptr;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
}


// The back buffer is the bottom of the stack - under OpenGL an empty draw buffer list IS the default
// framebuffer and that is all it takes. DX12 and Vulkan have to be told: there the back buffer needs
// its render target binding (DX) or a rendering scope plus the matching image layout (VK), and the one
// that a render target opens for itself must not sit inside it. Both calls are idempotent, and both
// return without doing anything while no frame is being recorded - which is what makes this safe in
// the setup phase, where render targets are activated long before the first frame.
//
// Runs AFTER a target was pushed on the stack and BEFORE its own Enable () (RenderTarget::Activate ()),
// so the back buffer's scope is closed before the target opens its own.

void DrawBufferHandler::SetActiveDrawBuffers(void) {
    gfxStates.SetDrawBuffers(m_activeBuffer ? m_activeBuffer->DrawBuffers() : DrawBufferList{});
#ifndef OPENGL
    if (m_activeBuffer)
        baseDisplayHandler.SuspendBackBuffer();
    else
        baseDisplayHandler.EnableBackBuffer();
#endif
}


void DrawBufferHandler::ActivateDrawBuffer(RenderTarget* buffer) {
    if (buffer != m_activeBuffer) {
        if (m_activeBuffer) {
            m_activeBuffer->Disable(false); // not a full, just a temporary deactivation
            m_drawBufferStack.Push(m_activeBuffer);
        }
        m_activeBuffer = buffer;
    }
    SetActiveDrawBuffers();
}


bool DrawBufferHandler::DeactivateDrawBuffer(RenderTarget* buffer) {
    if (buffer != m_activeBuffer)
        return false;
    m_activeBuffer->Disable();
    if ((m_suspendCount > 0) and (m_drawBufferStack.Length() <= m_suspendBase)) {
        m_activeBuffer = nullptr;
        return true;
    }
    if (m_drawBufferStack.IsEmpty())
        m_activeBuffer = nullptr;
    else {
        m_activeBuffer = m_drawBufferStack.Pop();
        m_activeBuffer->Reactivate();
    }
    SetActiveDrawBuffers();
    return true;
}


void DrawBufferHandler::ResetDrawBuffers(void) {
    while (m_activeBuffer)
        m_activeBuffer->Deactivate();
}


void DrawBufferHandler::SuspendDrawBuffers(void) {
    if (m_suspendCount++ > 0)
        return;
    if (m_activeBuffer) {
        m_activeBuffer->Disable(false);
        m_drawBufferStack.Push(m_activeBuffer);
        m_activeBuffer = nullptr;
    }
    m_suspendBase = m_drawBufferStack.Length();
}


void DrawBufferHandler::ResumeDrawBuffers(void) {
    if ((m_suspendCount <= 0) or (--m_suspendCount > 0))
        return;
    if (m_activeBuffer)
        return;
    if (not m_drawBufferStack.IsEmpty()) {
        m_activeBuffer = m_drawBufferStack.Pop();
        m_activeBuffer->Reactivate();
    }
    SetActiveDrawBuffers();
}

// =================================================================================================
