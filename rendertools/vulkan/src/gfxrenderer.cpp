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
#include "vkcontext.h"
#include "shader_compiler.h"
#include "pipeline_cache.h"
#include "descriptor_pool_handler.h"
#include "cbv_allocator.h"
#include "resource_handler.h"
#include "gfxapitype.h"
#include "image_layout_tracker.h"
#include "vkupload.h"	// CreateReadbackBuffer / one shot command buffer for ReadBuffer ()

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
    gfxStates.Init();
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
    m_frameCounter.Draw(true);
    Stop2DScene();
    UpdateFrameMetrics();
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
    //gfxStates.CheckError();
}


void GfxRenderer::FlushResources(void) noexcept {
    // Drains all pending setup-phase CommandLists and any deferred resource teardowns so that
    // the first BeginFrame can safely reset cbvAllocator and run gfxResourceHandler.Cleanup
    // without invalidating still-pending CommandBuffers. Called between Application::Setup
    // steps (analog to DX12 GfxRenderer::FlushResources → CommandListHandler::Flush).
    //
    // ExecuteAll(true) does the plain-submit-plus-WaitIdle variant (no frame-sync semaphores
    // or fence). Drain the deferred-cleanup lambdas for both frame slots, then clear the
    // CPU-side bind table: setup-phase Texture::Bind calls left stale handles in
    // m_boundSrvViews / m_boundSamplers / m_boundStorageBuffers; after Cleanup those handles
    // point at destroyed views/buffers, and the next render's vkUpdateDescriptorSets would
    // reject them.
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
    SavePipelineCache();
    GfxDataLayout::DestroyDefaultStreams();
    Shader::DestroyDefaultResources();
    meshHandler.Destroy();
    gfxResourceHandler.Cleanup(0);
    gfxResourceHandler.Cleanup(1);
}


void GfxRenderer::LoadPipelineCache(const String& shaderFolder) {
    pipelineCache.Load(shaderFolder);
}


void GfxRenderer::SavePipelineCache(void) {
    pipelineCache.Save();
}

// =================================================================================================
// The current swap chain image, as DrawScreen () left it - so call this before the present. The copy
// goes through a host visible readback buffer on a one shot command buffer, the way
// RenderTarget::ReadBuffer () does it; the image's layout is put back to what it was. Vulkan rows run
// top down and the swap chain is BGRA as a rule: the result is turned around to the bottom first RGBA
// order the OpenGL backend delivers.

bool GfxRenderer::ReadBuffer(void* buffer, size_t bufferSize, int x, int y, int width, int height) {
    VkImage image = baseDisplayHandler.CurrentBackBuffer();

    if (not (buffer and (image != VK_NULL_HANDLE)))
        return false;

    int w = baseDisplayHandler.GetWidth();
    int h = baseDisplayHandler.GetHeight();

    if (width <= 0)
        width = w - x;
    if (height <= 0)
        height = h - y;
    if ((x < 0) or (y < 0) or (width <= 0) or (height <= 0) or (x + width > w) or (y + height > h))
        return false;

    size_t rowBytes = size_t(width) * 4;
    size_t needed = rowBytes * size_t(height);

    if (bufferSize < needed)
        return false;

    VkStagingBuffer readback;

    if (not CreateReadbackBuffer(VkDeviceSize(needed), readback))
        return false;

    OneShotCommandBuffer cmd;

    if (not BeginSingleTimeCommands(cmd)) {
        readback.Destroy();
        return false;
    }

    ImageLayoutTracker& tracker = baseDisplayHandler.CurrentBackBufferTracker();
    VkImageLayout layoutBefore = tracker.Layout();

    tracker.ToTransferSrc(cmd.cb);

    // Vulkan rows run top down: the rectangle's bottom left (x, y) is row h - y - height from the top
    VkBufferImageCopy copy { };

    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;       // tightly packed
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = { int32_t(x), int32_t(h - y - height), 0 };
    copy.imageExtent = { uint32_t(width), uint32_t(height), 1 };

    vkCmdCopyImageToBuffer(cmd.cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
    // Back to the layout the frame left it in - the present, or the next draw, expects to find it there.
    if (layoutBefore == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        tracker.ToPresent(cmd.cb);
    else if (layoutBefore == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        tracker.ToColorAttachment(cmd.cb);
    if (not EndSingleTimeCommands(cmd)) {
        readback.Destroy();
        return false;
    }
    if (readback.mapped == nullptr) {
        readback.Destroy();
        return false;
    }

    // Host visible and coherent through VMA's AUTO mapping, so what the copy wrote is visible here.
    // Bottom row first, and BGRA swapped to RGBA where the swap chain is BGRA.
    const uint8_t* source = static_cast<const uint8_t*>(readback.mapped);
    uint8_t* dest = static_cast<uint8_t*>(buffer);
    VkFormat format = baseDisplayHandler.CurrentBackBufferFormat();
    bool bgra = (format == VK_FORMAT_B8G8R8A8_UNORM) or (format == VK_FORMAT_B8G8R8A8_SRGB);

    for (int row = 0; row < height; ++row) {
        const uint8_t* sourceRow = source + size_t(height - 1 - row) * rowBytes;
        uint8_t* destRow = dest + size_t(row) * rowBytes;

        if (not bgra)
            memcpy(destRow, sourceRow, rowBytes);
        else {
            for (size_t i = 0; i < rowBytes; i += 4) {
                destRow[i + 0] = sourceRow[i + 2];
                destRow[i + 1] = sourceRow[i + 1];
                destRow[i + 2] = sourceRow[i + 0];
                destRow[i + 3] = sourceRow[i + 3];
            }
        }
    }
    readback.Destroy();
    return true;
}

// =================================================================================================
