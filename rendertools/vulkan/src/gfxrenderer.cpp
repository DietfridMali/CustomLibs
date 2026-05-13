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
#include "vkcontext.h"
#include "shader_compiler.h"
#include "pipeline_cache.h"
#include "descriptor_pool_handler.h"
#include "cbv_allocator.h"
#include "resource_handler.h"
#include "gfxapitype.h"

// =================================================================================================
// Vulkan Renderer

bool GfxRenderer::InitGraphics(void) {
#   ifdef _DEBUG
    constexpr bool enableValidation = true;
#   else
    constexpr bool enableValidation = false;
#   endif

    SDL_Window* window = baseDisplayHandler.GetWindow();
    if (not window) {
        fprintf(stderr, "GfxRenderer::InitGraphics: SDL window not yet created. Vulkan path expects window-first init order\n");
        return false;
    }

    if (not vkContext.Create(window, enableValidation)) {
        fprintf(stderr, "Smiley-Battle: Cannot create Vulkan context.\n");
        return false;
    }
    if (not ShaderCompiler::Initialize()) {
        fprintf(stderr, "Smiley-Battle: Cannot initialize DXC shader compiler.\n");
        return false;
    }
    if (not pipelineCache.Create(vkContext.Device())) {
        fprintf(stderr, "Smiley-Battle: Cannot create Vulkan pipeline cache.\n");
        return false;
    }
    if (not descriptorPoolHandler.Create(vkContext.Device())) {
        fprintf(stderr, "Smiley-Battle: Cannot create Vulkan descriptor pools.\n");
        return false;
    }
    if (not cbvAllocator.Create()) {
        fprintf(stderr, "Smiley-Battle: Cannot create Vulkan UBO ring allocator.\n");
        return false;
    }
    if (not commandListHandler.Create(vkContext.Device(),
                                      vkContext.GraphicsQueue(), vkContext.PresentQueue(),
                                      vkContext.GraphicsFamily(), vkContext.PresentFamily(),
                                      "MainQueue")) {
        fprintf(stderr, "Smiley-Battle: Cannot create Vulkan CommandQueue.\n");
        return false;
    }
    if (not baseDisplayHandler.SetupSwapchain()) {
        fprintf(stderr, "Smiley-Battle: Cannot create Vulkan swapchain.\n");
        return false;
    }
    // Arm the first frame slot so subsequent BeginFrame paths have a valid sync state.
    if (not commandListHandler.CmdQueue().BeginFrame()) {
        fprintf(stderr, "Smiley-Battle: Cannot begin first Vulkan frame.\n");
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
    if (piggyback and m_temporaryList) {
        ++(m_temporaryList->m_refCounter);
        cl = m_temporaryList;
    }
    else {
        cl = commandListHandler.CreateCmdList(name, true);
        if (not cl)
            return nullptr;
        if (not cl->Open())
            return nullptr;
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
        if (flush)
            list->Flush();
        else
            list->Close();
        if (list == m_temporaryList)
            m_temporaryList = nullptr;
    }
    return true;
}


void GfxRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    if (not m_screenIsAvailable)
        return;
    ++m_frameIndex;
    m_frameCounter.Draw(true);
    Stop2DScene();
    m_screenIsAvailable = false;
    if (not m_screenBuffer)
        return;

    Set2DRenderStates();

    void* cl = StartOperation("DrawScreen", false);

    // Bring the swapchain back buffer to COLOR_ATTACHMENT and clear it before blitting.
    if (baseDisplayHandler.CurrentBackBuffer()) {
        baseDisplayHandler.EnableBackBuffer();
        gfxStates.ClearBackBuffer();
    }
    SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));

    m_renderTexture.m_handle = m_screenBuffer->BufferHandle(0);
    RenderToViewport(m_screenBuffer->GetAsTexture({}), ColorData::White, bRotate, bFlipVertically);

    // Transition back to PRESENT_SRC_KHR for vkQueuePresentKHR.
    if (baseDisplayHandler.CurrentBackBuffer())
        baseDisplayHandler.DisableBackBuffer();
    FinishOperation(cl);
    gfxStates.CheckError();
}


void GfxRenderer::FlushResources(void) noexcept {
    // Drains all pending setup-phase CommandLists and any deferred resource teardowns so that
    // the first BeginFrame can safely reset cbvAllocator and run gfxResourceHandler.Cleanup
    // without invalidating still-pending CommandBuffers. Called between Application::Setup
    // steps (analog to DX12 GfxRenderer::FlushResources → CommandListHandler::Flush).
    //
    // ExecuteAll(true) does the plain-submit-plus-WaitIdle variant (no frame-sync semaphores
    // or fence). Drain the deferred-cleanup lambdas for both frame slots, then clear the
    // CPU-side bind table: setup-phase Texture::Bind calls left stale VkImageView handles in
    // m_boundSrvViews / m_boundSamplers / m_boundStorageViews; after Cleanup those handles
    // point at destroyed views, and the next render's vkUpdateDescriptorSets would reject them.
    commandListHandler.ExecuteAll(true);
    gfxResourceHandler.Cleanup(0);
    gfxResourceHandler.Cleanup(1);
    commandListHandler.ResetBindings();
}


void GfxRenderer::Cleanup(void) noexcept {
    // WaitIdle ensures the GPU is no longer using any resources. Draining both frame-slot
    // cleanup queues afterwards executes callbacks deferred via gfxResourceHandler.TrackCleanup
    // (RT BufferInfo::Release, disposable textures, ...), so the underlying VkImage / VkImageView
    // / VmaAllocation handles are destroyed before gfxResourceHandler / vkContext are torn down.
    commandListHandler.CmdQueue().WaitIdle();
    gfxResourceHandler.Cleanup(0);
    gfxResourceHandler.Cleanup(1);
}

// =================================================================================================
