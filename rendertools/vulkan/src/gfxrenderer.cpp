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
        fprintf(stderr, "GfxRenderer::InitGraphics: SDL window not yet created — Vulkan path expects window-first init order\n");
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
    // TODO Phase C: commandListHandler.CmdQueue().BeginFrame() arms the first frame; needs
    // the per-frame command buffer pool from the full CommandList port.

    return true;
}


void* GfxRenderer::StartOperation(String name, bool piggyback) noexcept {
    // TODO Phase C: temp CommandList from commandListHandler — currently a no-op so that
    // upload paths (vkupload, gfxdatabuffer) which call StartOperation/FinishOperation as a
    // wrapper compile and run. vkupload uses its own one-shot CommandBuffer in Phase B.
    (void)name;
    (void)piggyback;
    return nullptr;
}


bool GfxRenderer::FinishOperation(void* cl, bool flush) noexcept {
    // TODO Phase C: commit the temporary CommandList (Close + register, or Flush).
    (void)cl;
    (void)flush;
    return true;
}


void GfxRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    // TODO Phase C: full screen-buffer blit path.
    //   Stop2DScene; SetViewport(window-rect); EnableBackBuffer (UNDEFINED→COLOR via tracker);
    //   render m_screenBuffer→backbuffer via RenderToViewport; DisableBackBuffer (→PRESENT);
    //   then BaseDisplayHandler::Update presents.
    (void)bRotate;
    (void)bFlipVertically;
    if (not m_screenIsAvailable)
        return;
    ++m_frameIndex;
    m_frameCounter.Draw(true);
    Stop2DScene();
    m_screenIsAvailable = false;
}

// =================================================================================================
