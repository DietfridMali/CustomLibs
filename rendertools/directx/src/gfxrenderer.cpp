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
#include "meshhandler.h"
#include "base_displayhandler.h"
#include "dx12context.h"
#include "gfxapitype.h"
#include "tracy_wrapper.h"
#include "resource_handler.h"
#include "renderstates.h"
#include "gfxrenderer.h"

#ifdef _DEBUG
static Texture* testTexture = nullptr;
#endif

#define LOG_OPERATIONS 0

// =================================================================================================
// DX12 Renderer

bool GfxRenderer::InitGraphics(void) {
#   if DBG_DIRECTX
    constexpr bool enableDebugLayer = true;
#   else
    constexpr bool enableDebugLayer = false;
#   endif
    if (not dx12Context.Create(enableDebugLayer)) {
        fprintf(stderr, "Cannot create DX12 device.\n");
        return false;
    }
    gfxStates.Init();
    if (not commandListHandler.Create(dx12Context.Device())) {
        fprintf(stderr, "Cannot create DX12 command queue.\n");
        return false;
    }
    if (not DescriptorHeapHandler::Instance().Create(dx12Context.Device())) {
        fprintf(stderr, "Cannot create DX12 descriptor heaps.\n");
        return false;
    }
    // Open the command list so displayHandler.Create() and renderer.Create()
    // can record initial state (viewport/scissor, resource barriers).
    // Init runs in slot 0 — all deferred RTV / resource pushes during setup land here and are
    // drained by the explicit Flush() calls between setup phases.
    if (not commandListHandler.BeginFrame(0)) {
        fprintf(stderr, "Cannot begin first DX12 frame.\n");
        return false;
    }
    if (not descriptorHeaps.CreateDefaultTextures(dx12Context.Device()))
        return false;
    return true;
}


void* GfxRenderer::StartOperation(String name, bool piggyback) noexcept {
    CommandList* cl = commandListHandler.CurrentCmdList();
    if (cl) {
        if (cl->IsTemporary())
            ++(cl->m_refCounter);
        return cl;
    }
    if (m_temporaryList and not (m_temporaryList->IsRecording() and (m_temporaryList->GetExecutionCounter() == m_temporaryExecution)))
        m_temporaryList = nullptr;
    if (piggyback and m_temporaryList) {
        ++(m_temporaryList->m_refCounter);
        cl = m_temporaryList;
    }
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
    if (piggyback) {
        m_temporaryList = cl;
        m_temporaryExecution = cl->GetExecutionCounter();
    }
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


void GfxRenderer::Cleanup(void) noexcept {
    // Flush all in-flight GPU work, then drain the deferred-release queue once while the
    // descriptor heaps are still alive. BeginShutdown() makes any further Track()/Cleanup()
    // inert — RenderTargets destroyed later (incl. at static destruction) must not touch the
    // deferred mechanism, whose singletons may already be gone.
    commandListHandler.CmdQueue().WaitIdle();
    SavePipelineCache();
    gfxResourceHandler.CleanupAll();
    GfxResourceHandler::BeginShutdown();
    GfxDataLayout::DestroyDefaultStreams();
    meshHandler.Destroy();
    Shader::DestroyRootSignature();
    commandListHandler.Destroy();
}


void GfxRenderer::LoadPipelineCache(const String& shaderFolder) {
    PSO::LoadPipelineLibrary(shaderFolder);
}


void GfxRenderer::SavePipelineCache(void) {
    PSO::SavePipelineLibrary();
}


void GfxRenderer::PrecreatePipelines(void) {
    PSO::PrecreatePSOs();
}


void GfxRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    ZoneScoped;
    if (not m_screenIsAvailable)
        return;
    m_frameCounter.Draw(true);
    Stop2DScene();
    UpdateFrameMetrics();
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
// The current swap chain back buffer, as DrawScreen () left it - so call this before the present. The
// copy goes through a readback heap the way RenderTarget::ReadBuffer () does it, the state of the
// back buffer is put back to what it was. DX rows run top down, the result is turned around to the
// bottom first order the OpenGL backend delivers.

bool GfxRenderer::ReadBuffer(void* buffer, size_t bufferSize, int x, int y, int width, int height) {
    ID3D12Resource* backBuffer = baseDisplayHandler.CurrentBackBuffer();
    ID3D12Device* device = dx12Context.Device();

    if (not (buffer and backBuffer and device))
        return false;

    int w = baseDisplayHandler.GetWidth();
    int h = baseDisplayHandler.GetHeight();

    if (width <= 0)
        width = w - x;
    if (height <= 0)
        height = h - y;
    if ((x < 0) or (y < 0) or (width <= 0) or (height <= 0) or (x + width > w) or (y + height > h))
        return false;
    if (bufferSize < size_t(width) * size_t(height) * 4)
        return false;

    D3D12_RESOURCE_DESC desc = backBuffer->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rowCount = 0;
    UINT64 rowSize = 0, totalSize = 0;

    // the footprint of the WHOLE subresource - GetCopyableFootprints () knows no rectangle; the copy
    // below moves only the box, the rest of the readback buffer stays untouched
    device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rowCount, &rowSize, &totalSize);

    D3D12_HEAP_PROPERTIES heapProps{};
    D3D12_RESOURCE_DESC readbackDesc{};

    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalSize;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> readback;

    if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
        return false;
#if DBG_DIRECTX
    {
        static const char name[] = "GfxRenderer::ReadBuffer readback";
        readback->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(sizeof(name) - 1), name);
    }
#endif

    CommandList* cl = commandListHandler.CreateCmdList(String("GfxRenderer::ReadBuffer"), true);

    if (not (cl and cl->Open(false)))
        return false;

    D3D12_RESOURCE_STATES stateBefore = baseDisplayHandler.CurrentBackBufferState();

    cl->SetBarrier(backBuffer, stateBefore, D3D12_RESOURCE_STATE_COPY_SOURCE);

    // DX rows run top down: the rectangle's bottom left (x, y) is row h - y - height from the top
    UINT top = UINT(h - y - height);
    D3D12_TEXTURE_COPY_LOCATION srcLoc{}, dstLoc{};
    D3D12_BOX box{ UINT(x), top, 0, UINT(x + width), top + UINT(height), 1 };

    srcLoc.pResource = backBuffer;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;
    dstLoc.pResource = readback.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = layout;
    if (ID3D12GraphicsCommandList* list = cl->GfxList())
        list->CopyTextureRegion(&dstLoc, UINT(x), top, 0, &srcLoc, &box);
    cl->SetBarrier(backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, stateBefore);
    cl->Close(false);
    commandListHandler.ExecutePending();

    uint8_t* source = nullptr;
    D3D12_RANGE readRange{ 0, size_t(totalSize) };

    if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void**>(&source))))
        return false;

    uint8_t* dest = static_cast<uint8_t*>(buffer);
    size_t rowBytes = size_t(width) * 4;

    // bottom row of the rectangle first, like glReadPixels
    for (int row = 0; row < height; ++row) {
        size_t sourceRow = size_t(top) + size_t(height - 1 - row);

        memcpy(dest + size_t(row) * rowBytes,
               source + size_t(layout.Offset) + sourceRow * size_t(layout.Footprint.RowPitch) + size_t(x) * 4,
               rowBytes);
    }

    D3D12_RANGE writeRange{ 0, 0 };   // nothing was written from the CPU side

    readback->Unmap(0, &writeRange);
    return true;
}

// =================================================================================================
