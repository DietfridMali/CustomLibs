#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "gfxdriverstates.h"
#include "drawbufferhandler.h"
#include "commandlist.h"
#include "base_displayhandler.h"

// =================================================================================================
// DX12 DrawBufferHandler
//
// Replaces glDrawBuffers / glBindFramebuffer with ID3D12GraphicsCommandList::OMSetRenderTargets.
// The RenderTarget class (Batch 5) exposes GetRTVHandles(), GetRTVCount(), GetDSVHandle() for this purpose.


void DrawBufferHandler::SetActiveDrawBuffers(void) {
    auto* list = commandListHandler.CurrentList();
    if (not list) return;

    if (m_drawBufferInfo.m_renderTarget) {
        // RenderTarget provides its own RTV/DSV handles — set them as render target.
        // RenderTarget::BindRenderTargets() will call OMSetRenderTargets internally (implemented in Batch 5).
        m_drawBufferInfo.m_renderTarget->BindRenderTargets(list);
    }
    else {
        // Default: render into the current swap chain back buffer.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = baseDisplayHandler.CurrentRTV();
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }
}


bool DrawBufferHandler::SetActiveBuffer(RenderTarget* buffer, bool clearBuffer) {
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


void DrawBufferHandler::ResetDrawBuffers(RenderTarget* activeBuffer, bool clearBuffer) {
    while (m_drawBufferStack.Length() > 0) {
        DrawBufferInfo info;
        m_drawBufferStack.Pop(info);
        if (info.m_renderTarget)
            info.m_renderTarget->Disable();
    }
    if (not SetActiveBuffer(activeBuffer, clearBuffer))
        SetActiveDrawBuffers();
}


void DrawBufferHandler::SaveDrawBuffer(void) {
    m_drawBufferStack.Push(m_drawBufferInfo);
}


void DrawBufferHandler::SetDrawBuffers(RenderTarget* renderTarget) {
    if ((renderTarget == nullptr) || (m_drawBufferInfo.m_renderTarget == nullptr) || (renderTarget != m_drawBufferInfo.m_renderTarget)) {
        SaveDrawBuffer();
        m_drawBufferInfo = DrawBufferInfo(renderTarget);
    }
    SetActiveDrawBuffers();
}


void DrawBufferHandler::RestoreDrawBuffer(void) {
    DrawBufferInfo info;
    m_drawBufferStack.Pop(info);
    m_drawBufferInfo = info;
    gfxDriverStates.BindTexture2D(0, 0); // clear slot 0 from the active SRV table
    if (m_drawBufferInfo.m_renderTarget)
        m_drawBufferInfo.m_renderTarget->Reenable(false, true);
    SetActiveDrawBuffers();
}


void DrawBufferHandler::RemoveDrawBuffer(RenderTarget* buffer) {
    for (auto& dbi : m_drawBufferStack)
        if (dbi.m_renderTarget == buffer)
            m_drawBufferStack.Remove(dbi);
}

// =================================================================================================
