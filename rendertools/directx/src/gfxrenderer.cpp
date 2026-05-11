#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "tristate.h"
#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "shadowmap.h"
#include "commandlist.h"
#include "base_displayhandler.h"
#include "dx12context.h"
#include "gfxapitype.h"
#include "tracy_wrapper.h"
#include "resource_handler.h"
#include "gfxrenderer.h"

#ifdef _DEBUG
static Texture* testTexture = nullptr;
#endif

#define LOG_OPERATIONS 0

// =================================================================================================
// DX12 Renderer

bool GfxRenderer::InitGraphics(void) {
#   ifdef _DEBUG
    constexpr bool enableDebugLayer = true;
#   else
    constexpr bool enableDebugLayer = false;
#   endif
    if (not dx12Context.Create(enableDebugLayer)) {
        fprintf(stderr, "Smiley-Battle: Cannot create DX12 device.\n");
        return false;
    }
    if (not commandListHandler.Create(dx12Context.Device())) {
        fprintf(stderr, "Smiley-Battle: Cannot create DX12 command queue.\n");
        return false;
    }
    if (not DescriptorHeapHandler::Instance().Create(dx12Context.Device())) {
        fprintf(stderr, "Smiley-Battle: Cannot create DX12 descriptor heaps.\n");
        return false;
    }
    // Open the command list so displayHandler.Create() and renderer.Create()
    // can record initial state (viewport/scissor, resource barriers).
    // Init runs in slot 0 — all deferred RTV / resource pushes during setup land here and are
    // drained by the explicit Flush() calls between setup phases.
    if (not commandListHandler.BeginFrame(0)) {
        fprintf(stderr, "Smiley-Battle: Cannot begin first DX12 frame.\n");
        return false;
    }
    return true;
}


void* GfxRenderer::StartOperation(String name, bool piggyback) noexcept {
    CommandList* cl = commandListHandler.CurrentCmdList();
    if (cl) {
        if (cl->IsTemporary())
            ++(cl->m_refCounter);
        return cl;
    }
    if (piggyback and m_temporaryList)
        ++(m_temporaryList->m_refCounter);
    else {
#if LOG_OPERATIONS
        fprintf(stderr, "Opening temp. CL '%s'\n", (const char*)name);
#endif
        cl = commandListHandler.CreateCmdList(name, true);
        if (not cl)
            return nullptr;
        if (not cl->Open()) {
            return nullptr;
        }
    }
    if (piggyback)
        m_temporaryList = cl;
    return cl;
}


bool GfxRenderer::FinishOperation(void* cl, bool flush) noexcept {
    CommandList* list = static_cast<CommandList*>(cl);
    if (not list)
        return false;
    if (not list->IsTemporary())
        return true;
#ifdef _DEBUG
    if (list->m_refCounter == 0)
        fprintf(stderr, "Invalid CL ref counter ('%s')\n", (const char*)list->GetName());
#endif
    if (--(list->m_refCounter) == 0) {
#if LOG_OPERATIONS
        fprintf(stderr, "Closing temp. CL '%s'\n", (const char*)list->GetName());
#endif
        if (flush)
            list->Flush();
        else
            list->Close();
        if (list == m_temporaryList)
            m_temporaryList = nullptr;
    }
    return true;
}


void GfxRenderer::FlushResources(void) noexcept {
    commandListHandler.Flush();
}


void GfxRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    ZoneScoped;
    if (not m_screenIsAvailable)
        return;
    ++m_frameIndex;
    m_frameCounter.Draw(true);
    Stop2DScene();
    m_screenIsAvailable = false;
    if (not m_screenBuffer)
        return;
    //m_screenBuffer->Deactivate();

    Set2DRenderStates();

    void* cl = StartOperation("DrawScreen", false);

    // Ensure the screen RenderTarget color buffer is in PSR state.
    // Stop2DScene() may have run with the list closed (first frame before BeginFrame),
    // in which case the RENDER_TARGET → PSR transition was never recorded.
    if (baseDisplayHandler.CurrentBackBuffer()) {
        baseDisplayHandler.EnableBackBuffer();
        gfxStates.ClearBackBuffer();
    }
    // Set viewport after the list is open so RSSetViewports is actually recorded.
    SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));

    m_renderTexture.m_handle = m_screenBuffer->BufferHandle(0);
    RenderToViewport(m_screenBuffer->GetAsTexture({}), ColorData::White, bRotate, bFlipVertically);

    // Blit screen RenderTarget to back buffer if not already done (e.g. ProgressIndicator skips DrawScreen).
    // No-op in the normal game loop where DrawScreen() was already called explicitly.
    // Safety: ensure back buffer is in PRESENT state (no-op if DrawScreen already did it).
    if (baseDisplayHandler.CurrentBackBuffer())
        baseDisplayHandler.DisableBackBuffer();
    FinishOperation(cl);
}

// =================================================================================================
