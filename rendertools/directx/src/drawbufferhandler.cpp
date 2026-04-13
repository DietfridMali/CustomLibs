#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "gfxdriverstates.h"
#include "drawbufferhandler.h"
#include "command_queue.h"
#include "base_displayhandler.h"

// =================================================================================================
// DX12 DrawBufferHandler
//
// Replaces glDrawBuffers / glBindFramebuffer with ID3D12GraphicsCommandList::OMSetRenderTargets.
// The FBO class (Batch 5) exposes GetRTVHandles(), GetRTVCount(), GetDSVHandle() for this purpose.


void DrawBufferHandler::SetActiveDrawBuffers(void) {
    auto* list = cmdQueue.List();
    if (!list) return;

    if (m_drawBufferInfo.m_fbo) {
        // FBO provides its own RTV/DSV handles — set them as render target.
        // FBO::BindRenderTargets() will call OMSetRenderTargets internally (implemented in Batch 5).
        m_drawBufferInfo.m_fbo->BindRenderTargets(list);
    }
    else {
        // Default: render into the current swap chain back buffer.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = baseDisplayHandler.CurrentRTV();
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }
}


bool DrawBufferHandler::SetActiveBuffer(FBO* buffer, bool clearBuffer) {
    if (m_activeBuffer != buffer) {
        if (m_activeBuffer)
            m_activeBuffer->Disable();
        m_activeBuffer = buffer;
    }
    return m_activeBuffer && m_activeBuffer->Reenable(clearBuffer);
}


void DrawBufferHandler::SetupDrawBuffers(void) {
    m_drawBufferInfo = DrawBufferInfo(nullptr);
    ResetDrawBuffers(nullptr);
}


void DrawBufferHandler::ResetDrawBuffers(FBO* activeBuffer, bool clearBuffer) {
    while (m_drawBufferStack.Length() > 0) {
        DrawBufferInfo info;
        m_drawBufferStack.Pop(info);
        if (info.m_fbo)
            info.m_fbo->Disable();
    }
    if (!SetActiveBuffer(activeBuffer, clearBuffer))
        SetActiveDrawBuffers();
}


void DrawBufferHandler::SaveDrawBuffer(void) {
    m_drawBufferStack.Push(m_drawBufferInfo);
}


void DrawBufferHandler::SetDrawBuffers(FBO* fbo) {
    if ((fbo == nullptr) || (m_drawBufferInfo.m_fbo == nullptr) || (fbo != m_drawBufferInfo.m_fbo)) {
        SaveDrawBuffer();
        m_drawBufferInfo = DrawBufferInfo(fbo);
    }
    SetActiveDrawBuffers();
}


void DrawBufferHandler::RestoreDrawBuffer(void) {
    DrawBufferInfo info;
    m_drawBufferStack.Pop(info);
    m_drawBufferInfo = info;
    gfxDriverStates.BindTexture2D(0, 0); // clear slot 0 from the active SRV table
    if (m_drawBufferInfo.m_fbo)
        m_drawBufferInfo.m_fbo->Reenable(false, true);
    SetActiveDrawBuffers();
}


void DrawBufferHandler::RemoveDrawBuffer(FBO* buffer) {
    for (auto& dbi : m_drawBufferStack)
        if (dbi.m_fbo == buffer)
            m_drawBufferStack.Remove(dbi);
}

// =================================================================================================
