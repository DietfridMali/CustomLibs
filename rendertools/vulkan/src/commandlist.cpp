#include "vkframework.h"
#include "commandlist.h"
#include "descriptor_pool_handler.h"
#include "cbv_allocator.h"
#include "resource_handler.h"
#include "gfxstates.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

// A lost device cannot be recovered from here: every later submit, wait and present fails as well, and
// an app that keeps recording runs into the driver and the validation layers with dead handles. So the
// first VK_ERROR_DEVICE_LOST ends the program with a message. Defined below, behind the second include
// block; vkupload.cpp declares it extern.
void HandleDeviceLost(VkResult res, const char* where) noexcept;

static constexpr double kStallFrameMinMs = 20.0;
static constexpr double kStallFrameFactor = 2.5;
static constexpr double kStallDetailMinMs = 1.0;
static constexpr size_t kStallDetailMaxChars = 16384;

struct VkStallEntry {
    const char* what;
    int         count;
    double      totalMs;
    double      maxMs;
};

struct VkStallRecorder {
    FILE*                       file { nullptr };
    bool                        openFailed { false };
    uint64_t                    frameNumber { 0 };
    double                      frameStartMs { 0.0 };
    double                      avgFrameMs { 0.0 };
    std::vector<VkStallEntry>   entries;
    std::string                 details;
};

static VkStallRecorder vkStalls;


double VkStallClock(void) noexcept
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


static FILE* VkStallFile(void) noexcept
{
    if ((vkStalls.file == nullptr) and not vkStalls.openFailed) {
        vkStalls.file = fopen("vkstalls.log", "wt");
        vkStalls.openFailed = (vkStalls.file == nullptr);
        if (vkStalls.file != nullptr) {
            std::time_t now = std::time(nullptr);
            char stamp[64] = "?";
            std::tm* local = std::localtime(&now);
            if (local != nullptr)
                std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", local);
            fprintf(vkStalls.file, "\n==== START %s ====\n", stamp);
            fflush(vkStalls.file);
        }
    }
    return vkStalls.file;
}


void VkStallNote(const char* what, double startMs, const char* detail) noexcept
{
    double ms = VkStallClock() - startMs;
    VkStallEntry* entry = nullptr;
    for (auto& e : vkStalls.entries) {
        if (std::strcmp(e.what, what) == 0) {
            entry = &e;
            break;
        }
    }
    if (entry == nullptr) {
        vkStalls.entries.push_back(VkStallEntry { what, 0, 0.0, 0.0 });
        entry = &vkStalls.entries.back();
    }
    ++entry->count;
    entry->totalMs += ms;
    if (entry->maxMs < ms)
        entry->maxMs = ms;
    if ((ms >= kStallDetailMinMs) and (vkStalls.details.size() < kStallDetailMaxChars)) {
        char line[256];
        snprintf(line, sizeof(line), "      %-28s %8.2f ms  %s\n", what, ms, detail ? detail : "");
        vkStalls.details += line;
    }
}


void VkStallEvent(const char* what, double startMs, const char* detail) noexcept
{
    double ms = VkStallClock() - startMs;
    VkStallNote(what, startMs, detail);
    FILE* f = VkStallFile();
    if (f == nullptr)
        return;
    fprintf(f, "frame %llu: %s %.2f ms  %s\n", (unsigned long long)vkStalls.frameNumber, what, ms, detail ? detail : "");
    fflush(f);
}


static void VkStallEndFrame(uint64_t nextFrame) noexcept
{
    double now = VkStallClock();
    if (vkStalls.frameStartMs > 0.0) {
        double frameMs = now - vkStalls.frameStartMs;
        bool isStall = (vkStalls.avgFrameMs > 0.0) and (frameMs > kStallFrameMinMs) and (frameMs > kStallFrameFactor * vkStalls.avgFrameMs);
        if (isStall) {
            FILE* f = VkStallFile();
            if (f != nullptr) {
                fprintf(f, "STALL frame %llu: %.2f ms (avg %.2f ms)\n", (unsigned long long)vkStalls.frameNumber, frameMs, vkStalls.avgFrameMs);
                for (const auto& e : vkStalls.entries)
                    fprintf(f, "    %-28s %5d x  total %8.2f ms  max %8.2f ms\n", e.what, e.count, e.totalMs, e.maxMs);
                fputs(vkStalls.details.c_str(), f);
                fflush(f);
            }
        }
        else if (vkStalls.avgFrameMs == 0.0)
            vkStalls.avgFrameMs = frameMs;
        else
            vkStalls.avgFrameMs = vkStalls.avgFrameMs * 0.95 + frameMs * 0.05;
    }
    vkStalls.entries.clear();
    vkStalls.details.clear();
    vkStalls.frameStartMs = now;
    vkStalls.frameNumber = nextFrame;
}

// CLs sind die wesentliche Datenstruktur zur Abwicklung von "Render Tasks".
// Render Tasks liegen immer zwischen open und close einer CL. Es gibt in dem Sinne keine verschachtelten Render-Tasks.
// Auch bei geschachteltem open - close von CLs wird die zuerst ausgeführt, die zuerst geschlossen wird - das liegt daran,
// dass diese zuerst in die pending-CL-Liste des CL-Handlers eingetragen wird. Dadurch wird der Vulkan-Port erleichtert.
// RenderTargets haben fixe CLs, diverse Detail-Tasks (i.d.R. Daten-Uploads) holen sich bei Bedarf eine temporäre CL.
// Temporäre CLs wandern nach Ausführung in einen Pool und werden bei Anforderung von temp.CLs bevorzugt verwendet.
// CLs verwalten auch Ressourcen, die im CL-Scope liegen, insb. PSOs für Shader.
// Temporäre CLs sind für den Fall in-Frame wiederholter Render Tasks für dasselbe Renderobjekt (i.d.R. Mesh-Datenpuffer) gedacht
// und nehmen der Objekt-Instanz die Aufgabe ab, hier eine eigene CL-Verwaltung zu implementieren, damit für jede solche Task
// auch eine CL verfügbar ist.

// =================================================================================================
// CommandQueue — Vulkan FrameSync wrapper.
//
// Holds the graphics + present queue handles, per-slot swapchain sync objects
// (imageAvailable, renderFinished, inFlight), and the round-robin frame slot index.
//
// Lifecycle:
//   Create            — record device + queue handles + family indices (no GPU work)
//   InitSyncObjects   — allocate per-slot semaphores + fences (after swapchain exists)
//   BeginFrame        — wait for slot fence, acquire next swapchain image
//   EndFrame          — present, advance frame slot
//   WaitIdle          — vkQueueWaitIdle on graphics queue (local; DX12-equivalent)
//   Destroy           — WaitIdle + destroy sync objects

bool CommandQueue::Create(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
                          uint32_t graphicsFamily, uint32_t presentFamily,
                          const String& name) noexcept
{
    if ((device == VK_NULL_HANDLE) or (graphicsQueue == VK_NULL_HANDLE) or (presentQueue == VK_NULL_HANDLE)) {
        fprintf(stderr, "CommandQueue::Create: null device or queue handle\n");
        return false;
    }
    m_device = device;
    m_graphicsQueue = graphicsQueue;
    m_presentQueue = presentQueue;
    m_graphicsFamily = graphicsFamily;
    m_presentFamily = presentFamily;
    (void)name;  // TODO: VK_EXT_debug_utils — vkSetDebugUtilsObjectNameEXT for queue handles
    return true;
}


bool CommandQueue::InitSyncObjects(VkSwapchainKHR swapchain) noexcept
{
    if (m_device == VK_NULL_HANDLE) {
        fprintf(stderr, "CommandQueue::InitSyncObjects: device not set, call Create first\n");
        return false;
    }
    if (swapchain == VK_NULL_HANDLE) {
        fprintf(stderr, "CommandQueue::InitSyncObjects: null swapchain\n");
        return false;
    }
    m_swapchain = swapchain;
    return CreateSyncObjects();
}


void CommandQueue::Destroy(void) noexcept
{
    WaitIdle();
    DestroySyncObjects();
    m_swapchain = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
    m_presentQueue = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
}


bool CommandQueue::BeginFrame(void) noexcept
{
    ++m_frameNumber;
    VkStallEndFrame(m_frameNumber);
    // Wait until the GPU has finished using this frame slot.
    double stallStart = VkStallClock();
    VkResult res = vkWaitForFences(m_device, 1, &m_inFlight[m_frameIndex], VK_TRUE, UINT64_MAX);
    VkStallNote("frame fence wait", stallStart, nullptr);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "CommandQueue::BeginFrame: vkWaitForFences failed (%d)\n", (int)res);
        HandleDeviceLost(res, "CommandQueue::BeginFrame");
        return false;
    }
    res = vkResetFences(m_device, 1, &m_inFlight[m_frameIndex]);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "CommandQueue::BeginFrame: vkResetFences failed (%d)\n", (int)res);
        return false;
    }
    // The slot's resources hang on its fence alone, so they are reset before the image is acquired -
    // a frame whose acquire fails still records, and must not do so on top of the slot's last cycle.
    stallStart = VkStallClock();
    gfxResourceHandler.Cleanup(m_frameIndex);
    VkStallNote("frame resource cleanup", stallStart, nullptr);
    stallStart = VkStallClock();
    descriptorPoolHandler.BeginFrame(m_frameIndex);
    VkStallNote("descriptor pool reset", stallStart, nullptr);
    cbvAllocator.Reset(m_frameIndex);
    stallStart = VkStallClock();
    bool acquired = AcquireNextImage();
    VkStallNote("acquire next image", stallStart, nullptr);
    return acquired;
}


void CommandQueue::EndFrame(void) noexcept
{
    Present();
    m_frameIndex = (m_frameIndex + 1) % FRAME_COUNT;
}


void CommandQueue::WaitIdle(void) noexcept
{
    if (m_graphicsQueue == VK_NULL_HANDLE)
        return;
    double stallStart = VkStallClock();
    VkResult res = vkQueueWaitIdle(m_graphicsQueue);
    VkStallNote("queue wait idle", stallStart, nullptr);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "CommandQueue::WaitIdle: vkQueueWaitIdle failed (%d)\n", (int)res);
        HandleDeviceLost(res, "CommandQueue::WaitIdle");
    }
}


VkCommandBuffer CommandQueue::CmdBuffer(void) const noexcept
{
    return commandListHandler.CurrentGfxList();
}

// =================================================================================================
// CommandQueue — private helpers

bool CommandQueue::CreateSyncObjects(void) noexcept
{
    VkSemaphoreCreateInfo semInfo{ };
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{ };
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // first BeginFrame must not deadlock on the wait

    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        VkResult r1 = vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailable[i]);
        VkResult r2 = vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlight[i]);
        if ((r1 != VK_SUCCESS) or (r2 != VK_SUCCESS)) {
            fprintf(stderr, "CommandQueue::CreateSyncObjects: failed at slot %u (sem=%d fence=%d)\n",
                    i, (int)r1, (int)r2);
            return false;
        }
    }
    // renderFinished is per swapchain image, not per frame slot. Allocate up to the static
    // upper bound MAX_BACK_BUFFERS — extra slots beyond the swapchain's actual ImageCount are
    // harmless since the present/submit paths only ever index by m_imageIndex < ImageCount.
    for (uint32_t i = 0; i < Swapchain::MAX_BACK_BUFFERS; ++i) {
        VkResult r = vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinished[i]);
        if (r != VK_SUCCESS) {
            fprintf(stderr, "CommandQueue::CreateSyncObjects: vkCreateSemaphore(renderFinished[%u]) failed (%d)\n",
                    i, (int)r);
            return false;
        }
    }
    return true;
}


void CommandQueue::DestroySyncObjects(void) noexcept
{
    if (m_device == VK_NULL_HANDLE)
        return;
    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        if (m_imageAvailable[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, m_imageAvailable[i], nullptr);
            m_imageAvailable[i] = VK_NULL_HANDLE;
        }
        if (m_inFlight[i] != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, m_inFlight[i], nullptr);
            m_inFlight[i] = VK_NULL_HANDLE;
        }
    }
    for (uint32_t i = 0; i < Swapchain::MAX_BACK_BUFFERS; ++i) {
        if (m_renderFinished[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, m_renderFinished[i], nullptr);
            m_renderFinished[i] = VK_NULL_HANDLE;
        }
    }
    m_acquireWaitPending = false;
}


bool CommandQueue::RecreateSyncObjects(void) noexcept
{
    if (m_device == VK_NULL_HANDLE)
        return false;
    DestroySyncObjects();
    return CreateSyncObjects();
}


bool CommandQueue::AcquireNextImage(void) noexcept
{
    VkResult res = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                         m_imageAvailable[m_frameIndex], VK_NULL_HANDLE,
                                         &m_imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain is stale (e.g. window resized). Caller is BaseDisplayHandler;
        // it owns the swapchain and is expected to recreate it. Phase B.
        fprintf(stderr, "CommandQueue::AcquireNextImage: VK_ERROR_OUT_OF_DATE_KHR\n");
        return false;
    }
    if ((res != VK_SUCCESS) and (res != VK_SUBOPTIMAL_KHR)) {
        fprintf(stderr, "CommandQueue::AcquireNextImage: vkAcquireNextImageKHR failed (%d)\n", (int)res);
        HandleDeviceLost(res, "CommandQueue::AcquireNextImage");
        return false;
    }
    m_acquireWaitPending = true;
    return true;
}


bool CommandQueue::TakeAcquireWait(VkSemaphoreSubmitInfo& waitInfo) noexcept
{
    if (not m_acquireWaitPending)
        return false;
    m_acquireWaitPending = false;
    waitInfo = VkSemaphoreSubmitInfo { };
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = SubmitWaitSemaphore();
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    return true;
}


void CommandQueue::Present(void) noexcept
{
    ZoneScopedN("CmdQueue::Present");
    VkPresentInfoKHR present { };
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_renderFinished[m_imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &m_imageIndex;

    double stallStart = VkStallClock();
    VkResult res = vkQueuePresentKHR(m_presentQueue, &present);
    VkStallNote("present", stallStart, nullptr);
    if ((res != VK_SUCCESS) and (res != VK_SUBOPTIMAL_KHR) and (res != VK_ERROR_OUT_OF_DATE_KHR)) {
        fprintf(stderr, "CommandQueue::Present: vkQueuePresentKHR failed (%d)\n", (int)res);
        HandleDeviceLost(res, "CommandQueue::Present");
    }
}

// =================================================================================================

// =================================================================================================
// CommandList - Vulkan implementation.
//
// 1:1 port of the DX12 CommandList: each CL owns FRAME_COUNT VkCommandPools and one CB per pool.
// Open() switches to the active frame's pool/CB, vkResetCommandPool then vkBeginCommandBuffer.
// Close() ends recording. Flush() submits + waits idle for one-shot (temp) lists.

#include "shader.h"
#include "vkcontext.h"
#include "gfxstates.h"
#include "gfxrenderer.h"
#include "pipeline_cache.h"
#include "rendertarget.h"
#include "base_displayhandler.h"

#include <cstdlib>

void HandleDeviceLost(VkResult res, const char* where) noexcept
{
    if (res != VK_ERROR_DEVICE_LOST)
        return;
    fprintf(stderr, "%s: VK_ERROR_DEVICE_LOST - graphics device lost, terminating\n", where);
    fflush(stderr);
    // The window goes first - a message box behind a fullscreen window cannot be seen or answered.
    if (SDL_Window* window = baseDisplayHandler.GetWindow())
        SDL_HideWindow(window);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Internal Error", "Graphics device lost", nullptr);
    // No exit (): static destructors and atexit handlers would walk back into Vulkan with the dead device.
    std::_Exit(1);
}

List<RenderStates> CommandList::m_renderStateStack;


uint32_t CommandList::ActiveFrameIndex(void) noexcept
{
    return commandListHandler.CmdQueue().FrameIndex();
}


void CommandList::PushRenderStates(void) noexcept
{
    m_renderStateStack.Push(baseRenderer.RenderStates());
}


void CommandList::PopRenderStates(void) noexcept
{
    if (m_renderStateStack.Length() > 0)
        baseRenderer.RenderStates() = m_renderStateStack.Pop();
}


bool CommandList::Create(const String& name, bool isTemporary) noexcept
{
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return false;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = vkContext.GraphicsFamily();
    // RESET_COMMAND_BUFFER_BIT: these per-frame / temporary command buffers are re-recorded every
    // frame (ONE_TIME_SUBMIT, so they become invalid after execution). Open() resets the whole pool
    // via vkResetCommandPool, but a recycled/temporary list can reach vkBeginCommandBuffer with its CB
    // not in the initial state — the bit makes that (implicit) per-buffer reset legal instead of a
    // validation error. TRANSIENT_BIT stays as the short-lived-buffers allocator hint.
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        VkResult res = vkCreateCommandPool(device, &poolInfo, nullptr, &m_pools[i]);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "CommandList::Create: vkCreateCommandPool[%u] failed (%d)\n", i, (int)res);
            return false;
        }
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_pools[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        res = vkAllocateCommandBuffers(device, &allocInfo, &m_cmdBuffers[i]);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "CommandList::Create: vkAllocateCommandBuffers[%u] failed (%d)\n", i, (int)res);
            return false;
        }
    }
    m_id = commandListHandler.m_cmdListId++;
    m_name = name;
    m_isTemporary = isTemporary;
    return true;
}


void CommandList::Destroy(void) noexcept
{
    m_isRecording = false;
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return;
    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        if (m_pools[i] != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, m_pools[i], nullptr);
            m_pools[i] = VK_NULL_HANDLE;
            m_cmdBuffers[i] = VK_NULL_HANDLE;
        }
    }
}


void CommandList::Reset(void) noexcept
{
    m_refCounter = 1;
    m_isFlushed = false;
    m_isRecording = false;
}


bool CommandList::Open(bool saveRenderStates, bool detached) noexcept
{
    if (m_isRecording)
        return true;
    m_isDetached = detached;
    uint32_t fi = ActiveFrameIndex();
    if ((m_pools[fi] == VK_NULL_HANDLE) or (m_cmdBuffers[fi] == VK_NULL_HANDLE))
        return false;
    VkResult res = vkResetCommandPool(vkContext.Device(), m_pools[fi], 0);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "CommandList::Open: vkResetCommandPool failed (%d)\n", (int)res);
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    res = vkBeginCommandBuffer(m_cmdBuffers[fi], &bi);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "CommandList::Open: vkBeginCommandBuffer failed (%d)\n", (int)res);
        return false;
    }
    m_isRecording = true;
    m_isFlushed = false;
    m_usesBackBuffer = false;
    m_openSerial = gfxResourceHandler.NextSerial();
    m_activePipeline = VK_NULL_HANDLE;
    ++m_executionCounter;
    if (detached)
        commandListHandler.Register(this);
    else
        commandListHandler.PushCmdList(this);
    // Track as open. Close() will register the CL in m_pendingLists at close-order,
    // which is what the submit sequence uses. ExecuteAll forces a Close on anything
    // still in m_openLists at frame end.
    commandListHandler.m_openLists.Push(this);
#if USE_TRACY
    m_gpuZone = new tracy::VkCtxScope(commandListHandler.m_gpuProfilerCtx,
        uint32_t(__LINE__), __FILE__, strlen(__FILE__), __FUNCTION__, strlen(__FUNCTION__),
        (const char*)m_name, size_t(m_name.Length()), m_cmdBuffers[fi], true);
#endif
    if (detached)
        return true;
    if (saveRenderStates)
        PushRenderStates();
    gfxStates.RestoreViewport();
#ifdef _DEBUG
    //gfxStates.CheckError();
#endif
    return true;
}


void CommandList::Close(bool restoreRenderStates) noexcept
{
    if (not m_isRecording)
        return;
    m_isRecording = false;
#if USE_TRACY
    delete m_gpuZone;
    m_gpuZone = nullptr;
#endif
    uint32_t fi = ActiveFrameIndex();
    VkResult res = vkEndCommandBuffer(m_cmdBuffers[fi]);
    if (res != VK_SUCCESS)
        fprintf(stderr, "CommandList::Close: vkEndCommandBuffer failed (%d)\n", (int)res);
#ifdef _DEBUG
    gfxStates.CheckError((const char*)m_name);
#endif
    if (m_isDetached) {
        commandListHandler.Register(this);
        return;
    }
    commandListHandler.PopCmdList();
    // Register in pendingLists in close-order. ExecuteAll iterates pendingLists in
    // this order, so the CL whose Close() ran first is submitted first.
    commandListHandler.Register(this);
    if (restoreRenderStates)
        PopRenderStates();
}


void CommandList::Flush(void) noexcept
{
    if (m_isFlushed)
        return;
    m_isFlushed = true;
    double stallStart = VkStallClock();
    Close();

    uint32_t fi = ActiveFrameIndex();
    VkCommandBufferSubmitInfo cbInfos[2]{};
    uint32_t cbCount = 0;
    CommandList* uploadList = commandListHandler.m_uploadList;
    if (uploadList and (uploadList != this)) {
        uploadList->m_isFlushed = true;
        uploadList->Close();
        commandListHandler.m_uploadList = nullptr;
        cbInfos[cbCount].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cbInfos[cbCount].commandBuffer = uploadList->m_cmdBuffers[fi];
        ++cbCount;
    }
    cbInfos[cbCount].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbInfos[cbCount].commandBuffer = m_cmdBuffers[fi];
    ++cbCount;

    VkSubmitInfo2 submit{};
    submit.sType  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = cbCount;
    submit.pCommandBufferInfos = cbInfos;

    VkSemaphoreSubmitInfo waitInfo{};
    bool usesBackBuffer = m_usesBackBuffer or (uploadList and (uploadList != this) and uploadList->m_usesBackBuffer);
    if (usesBackBuffer and commandListHandler.CmdQueue().TakeAcquireWait(waitInfo)) {
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &waitInfo;
    }

    VkResult res = vkQueueSubmit2(commandListHandler.GetQueue(), 1, &submit, VK_NULL_HANDLE);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "CommandList::Flush: vkQueueSubmit2 failed (%d)\n", (int)res);
        HandleDeviceLost(res, "CommandList::Flush");
    }
#ifdef _DEBUG
    CheckDeviceRemoved("Flush");
#endif
    commandListHandler.CmdQueue().WaitIdle();
    DisposeResources();
    String name = GetName();
    VkStallNote("command list flush", stallStart, static_cast<const char*>(name));
}


void CommandList::SetBarrier(VkImage image, ImageLayoutTracker& tracker, VkImageLayout newLayout,
                             VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    if (not m_isRecording or (image == VK_NULL_HANDLE))
        return;
    tracker.TransitionTo(GfxList(), newLayout, dstStage, dstAccess);
#ifdef _DEBUG
    //gfxStates.CheckError();
#endif
}


void CommandList::SetBarrier(const VkImageMemoryBarrier2* barriers, int count)
{
    if (not m_isRecording or not barriers or (count <= 0))
        return;
    VkDependencyInfo dep{};
    dep.sType   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = uint32_t(count);
    dep.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(GfxList(), &dep);
#ifdef _DEBUG
    //gfxStates.CheckError();
#endif
}


void CommandList::DisposeResources(void) noexcept
{
    for (auto& fn : m_disposableResources)
        fn();
    m_disposableResources.Clear();
}


void CommandList::SetActivePipeline(VkPipeline pipeline, Shader* /*shader*/) noexcept
{
    if (pipeline != m_activePipeline) {
        if (m_isRecording and pipeline != VK_NULL_HANDLE)
            vkCmdBindPipeline(GfxList(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        m_activePipeline = pipeline;
#ifdef _DEBUG
        //gfxStates.CheckError();
#endif
    }
}


static RenderStates lastPipelineStates;
static CommandList* lastPipelineList = nullptr;
static Shader* lastPipelineShader = nullptr;

bool ResolveDrawPipeline(CommandList* cl, Shader* shader) noexcept
{
    if ((cl->m_activePipeline != VK_NULL_HANDLE) and (cl == lastPipelineList) and (shader == lastPipelineShader)
        and (baseRenderer.RenderStates() == lastPipelineStates))
        return true;
    return cl->GetPipeline(shader) != VK_NULL_HANDLE;
}


VkPipeline CommandList::GetPipeline(Shader* shader) noexcept
{
    // PipelineKey {shader, RenderStates, colour/depth formats}.
    //
    // Source of truth for colorAttachmentCount is shader->m_dataLayout.m_numRenderTargets
    // (= number of SV_Target outputs the pixel shader writes), matching DX12 PSO behaviour.
    // The render-pass scope may have more color attachments than the shader writes; the
    // VK_EXT_dynamic_rendering_unused_attachments feature (enabled in VKContext::CreateDevice)
    // makes that mismatch legal.
    //
    // Color formats come from the active render surface — the active RenderTarget when one is
    // bound (RT::FillPipelineKey), or the swapchain back buffer in DrawScreen-style passes.
    // Whichever it is, we then truncate the format list to numRenderTargets so the pipeline
    // is built with exactly the slots the shader actually writes.
    PipelineKey key{};
    key.shader = shader;
    key.states = baseRenderer.RenderStates();
    if (RenderTarget* rt = baseRenderer.GetActiveBuffer())
        rt->FillPipelineKey(key);
    else if (baseDisplayHandler.IsInRendering()) {
        key.colorFormats[0] = baseDisplayHandler.m_swapchain.Format();
        key.colorFormatCount = 1;
        key.depthFormat = VK_FORMAT_UNDEFINED;
    }

    const uint32_t numRT = uint32_t(shader->m_dataLayout.m_numRenderTargets);
    if (numRT < key.colorFormatCount) {
        for (uint32_t i = numRT; i < key.colorFormatCount; ++i)
            key.colorFormats[i] = VK_FORMAT_UNDEFINED;
        key.colorFormatCount = numRT;
    }

    VkPipeline p = pipelineCache.GetOrCreate(key);
    if (p != VK_NULL_HANDLE) {
        SetActivePipeline(p, shader);
        if (m_isRecording)
            key.states.SetDynamicStates(GfxList());
        m_activeTopology = key.states.topology;
        lastPipelineStates = key.states;
        lastPipelineList = this;
        lastPipelineShader = shader;
    }
    return p;
}


bool CommandList::SetTopology(Shader* shader, MeshTopology topology) noexcept
{
    if (m_activeTopology == uint8_t(topology))
        return true;
    baseRenderer.RenderStates().topology = uint8_t(topology);
    return GetPipeline(shader) != VK_NULL_HANDLE;
}


#ifdef _DEBUG
void CommandList::CheckDeviceRemoved(const char* context) noexcept
{
    //gfxStates.CheckError();
}
#endif

// =================================================================================================
// CommandListHandler

#ifdef _DEBUG
bool CommandListHandler::s_logCalls = false;
#endif


bool CommandListHandler::Create(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
                                uint32_t graphicsFamily, uint32_t presentFamily,
                                const String& name) noexcept
{
    if (not m_cmdQueue.Create(device, graphicsQueue, presentQueue, graphicsFamily, presentFamily, name))
        return false;
#if USE_TRACY
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // RESET_COMMAND_BUFFER_BIT: TracyVkContext's VkCtx constructor calibrates by re-recording the
    // SAME cmdbuf three times (begin/end/submit/wait), without resetting it between begins. Begins #2
    // and #3 are implicit per-buffer resets, which need this bit (else VUID-vkBeginCommandBuffer-00050).
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily;
    VkCommandPool tracyPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &tracyPool) == VK_SUCCESS) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = tracyPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer tracyCb = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &allocInfo, &tracyCb) == VK_SUCCESS)
            m_gpuProfilerCtx = TracyVkContext(vkContext.PhysicalDevice(), device, graphicsQueue, tracyCb);
        vkDestroyCommandPool(device, tracyPool, nullptr);
    }
#endif
    return true;
}


void CommandListHandler::Destroy(void) noexcept
{
    if (m_gpuProfilerCtx) {
        TracyVkDestroy(m_gpuProfilerCtx);
        m_gpuProfilerCtx = nullptr;
    }
    for (auto cl : m_recycledLists) {
        cl->Destroy();
        delete cl;
    }
    m_recycledLists.Clear();
    m_cmdQueue.Destroy();
}


void CommandListHandler::PushCmdList(CommandList* cl) noexcept
{
    if (m_currentListData.cmdList)
        m_cmdListStack.Push(m_currentListData);
    m_currentListData = CommandListData{ cl, cl->GfxList() };
}


void CommandListHandler::PopCmdList(void) noexcept
{
    m_currentListData = (m_cmdListStack.Length() > 0) ? m_cmdListStack.Pop() : CommandListData();
}


void CommandListHandler::Register(CommandList* cl) noexcept
{
    if (not cl)
        return;
#ifdef _DEBUG
    if (cl->m_name.IsEmpty())
        fprintf(stderr, "CommandListHandler::Register: Unnamed command list\n");
#endif
    for (auto l : m_pendingLists)
        if (cl == l)
            return;
    m_pendingLists.Push(cl);
}


void CommandListHandler::ExecuteAll(bool intermediate) noexcept
{
    ZoneScopedN("ExecuteAll");
    // No command buffer may be submitted with an open rendering scope, and the back buffer's belongs
    // to nobody in particular - it is opened where the draw buffer stack runs empty
    // (DrawBufferHandler::SetActiveDrawBuffers ()) and would otherwise still stand here in a setup
    // phase drain. Closing it is free when there is none.
    baseDisplayHandler.SuspendBackBuffer();
    // intermediate=false (default): frame-end submit — binds the swapchain frame-sync triplet
    //   (imageAvailable wait unless an earlier submit of this frame has taken it, renderFinished
    //   signal, inFlight fence). Must be called between a
    //   prior BeginFrame (which signaled imageAvailable via vkAcquireNextImageKHR and reset the
    //   inFlight fence) and a subsequent Present (which waits on renderFinished).
    // intermediate=true: setup-phase or mid-init drain — plain submit without renderFinished and
    //   inFlight, waiting on imageAvailable only when it carries back buffer commands,
    //   followed by vkQueueWaitIdle. Lets setup-phase CommandLists go to the GPU and finish
    //   before the first BeginFrame resets cbvAllocator / drains gfxResourceHandler.
    // Force-close any CLs still recording. Their Close() registers them at the end of
    // m_pendingLists, so they get submitted last — which matches the architectural
    // contract that the outermost (last-to-close) CL runs after all the inner ones
    // whose Close() already happened during frame rendering.
    while (m_openLists.Length() > 0) {
        CommandList* l = m_openLists.Pop();
        if (l->IsRecording())
            l->Close();
    }
    m_uploadList = nullptr;

    if (m_pendingLists.IsEmpty())
        return;

    AutoArray<VkCommandBufferSubmitInfo> cbInfos(m_pendingLists.Length());
    int n = 0;
    bool usesBackBuffer = false;
    for (auto l : m_pendingLists) {
        if (l->IsFlushed())
            continue;
        VkCommandBufferSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        info.commandBuffer = l->GfxList(true);
        cbInfos[n++] = info;
        if (l->m_usesBackBuffer)
            usesBackBuffer = true;
    }
    if (n > 0) {
        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.commandBufferInfoCount = uint32_t(n);
        submit.pCommandBufferInfos = cbInfos.Data();

        VkSemaphoreSubmitInfo waitInfo{};
        VkSemaphoreSubmitInfo signalInfo{};
        VkFence fence = VK_NULL_HANDLE;
        if ((usesBackBuffer or not intermediate) and m_cmdQueue.TakeAcquireWait(waitInfo)) {
            submit.waitSemaphoreInfoCount = 1;
            submit.pWaitSemaphoreInfos = &waitInfo;
        }
        if (not intermediate) {
            signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfo.semaphore = m_cmdQueue.SubmitSignalSemaphore();
            signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            submit.signalSemaphoreInfoCount = 1;
            submit.pSignalSemaphoreInfos = &signalInfo;
            fence = m_cmdQueue.SubmitSignalFence();
        }

        {
            ZoneScopedN("vkQueueSubmit2");
            double stallStart = VkStallClock();
            VkResult res = vkQueueSubmit2(m_cmdQueue.GraphicsQueue(), 1, &submit, fence);
            VkStallNote(intermediate ? "intermediate submit" : "frame submit", stallStart, nullptr);
            if (res != VK_SUCCESS) {
                fprintf(stderr, "CommandListHandler::ExecuteAll: vkQueueSubmit2 failed (%d)\n", (int)res);
                HandleDeviceLost(res, "CommandListHandler::ExecuteAll");
            }
        }
    }
#ifdef _DEBUG
    gfxStates.CheckError("CommandListHandler::ExecuteAll submit");
#endif
    for (auto l : m_pendingLists) {
        if (l->IsTemporary())
            m_recycledLists.Push(l);
    }
    m_pendingLists.Clear();
    m_cmdListStack.Clear();

    if (intermediate)
        m_cmdQueue.WaitIdle();
}


void CommandListHandler::ExecutePending(void) noexcept
{
    ZoneScopedN("ExecutePending");
    double stallStart = VkStallClock();
    // The upload list is registered when it is opened, so that it runs ahead of the frame - it is the one
    // pending list that may still be recording. It goes out with the rest; the next upload opens a new one.
    if (m_uploadList) {
        m_uploadList->Close();
        m_uploadList = nullptr;
    }
    if (m_pendingLists.IsEmpty())
        return;

    AutoArray<VkCommandBufferSubmitInfo> cbInfos(m_pendingLists.Length());
    int n = 0;
    bool usesBackBuffer = false;
    for (auto l : m_pendingLists) {
        if (l->IsFlushed())
            continue;
        VkCommandBufferSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        info.commandBuffer = l->GfxList(true);
        cbInfos[n++] = info;
        if (l->m_usesBackBuffer)
            usesBackBuffer = true;
    }
    if (n > 0) {
        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.commandBufferInfoCount = uint32_t(n);
        submit.pCommandBufferInfos = cbInfos.Data();

        VkSemaphoreSubmitInfo waitInfo{};
        if (usesBackBuffer and m_cmdQueue.TakeAcquireWait(waitInfo)) {
            submit.waitSemaphoreInfoCount = 1;
            submit.pWaitSemaphoreInfos = &waitInfo;
        }

        VkResult res = vkQueueSubmit2(m_cmdQueue.GraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "CommandListHandler::ExecutePending: vkQueueSubmit2 failed (%d)\n", (int)res);
            HandleDeviceLost(res, "CommandListHandler::ExecutePending");
        }
    }
#ifdef _DEBUG
    gfxStates.CheckError("CommandListHandler::ExecutePending submit");
#endif
    m_cmdQueue.WaitIdle();
#if USE_TRACY
    if (m_gpuProfilerCtx)
        TracyVkCollectHost(m_gpuProfilerCtx);
#endif
    for (auto l : m_pendingLists) {
        if (l->IsTemporary())
            m_recycledLists.Push(l);
    }
    m_pendingLists.Clear();
    DrainFrameResources();
    VkStallNote("execute pending", stallStart, nullptr);
}


void CommandListHandler::DrainFrameResources(void) noexcept
{
    AutoArray<CommandList*> recordingLists;
    uint64_t oldestOpenSerial = UINT64_MAX;

    for (auto l : m_openLists) {
        if (not l->IsRecording())
            continue;
        recordingLists.Push(l);
        if (l->m_openSerial < oldestOpenSerial)
            oldestOpenSerial = l->m_openSerial;
    }
    m_openLists.Clear();
    for (auto l : recordingLists)
        m_openLists.Push(l);

    uint32_t frameIndex = m_cmdQueue.FrameIndex();

    gfxResourceHandler.CleanupBefore(frameIndex, oldestOpenSerial);
    if (gfxResourceHandler.LastAllocSerial() < oldestOpenSerial) {
        descriptorPoolHandler.BeginFrame(frameIndex);
        cbvAllocator.Reset(frameIndex);
    }
    ResetBindings();
}


CommandList* CommandListHandler::CreateCmdList(const String& name, bool isTemporary) noexcept
{
    if (isTemporary and not m_recycledLists.IsEmpty()) {
        CommandList* cl = m_recycledLists.Pop();
        cl->SetName(name);
        cl->Reset();
        return cl;
    }
    CommandList* cl = new CommandList();
    if (not cl->Create(name, isTemporary)) {
        delete cl;
        return nullptr;
    }
    cl->Reset();
    ++m_cmdListCount;
    return cl;
}


bool CommandListHandler::UsesOrderedCopyList(void) noexcept
{
    return baseRenderer.DrawBuffersSuspended();
}


CommandList* CommandListHandler::OpenOrderedCopyList(void) noexcept
{
    if (not UsesOrderedCopyList())
        return nullptr;
    CommandList* cl = CreateCmdList(String("OrderedCopy"), true);
    if (not (cl and cl->Open(false)))
        return nullptr;
    return cl;
}


bool CommandListHandler::IsInRendering(void) noexcept
{
    VkCommandBuffer cb = CurrentGfxList();
    if (cb == VK_NULL_HANDLE)
        return false;
    if (baseDisplayHandler.IsInRendering() and (baseDisplayHandler.m_backBufferCb == cb))
        return true;
    RenderTarget* rt = baseRenderer.GetActiveBuffer();
    return rt and rt->m_isInRendering and rt->m_cmdList and (rt->m_cmdList->GfxList() == cb);
}


CommandListHandler::RenderingScope CommandListHandler::SuspendRendering(void) noexcept
{
    RenderingScope scope;
    VkCommandBuffer cb = CurrentGfxList();
    if (cb == VK_NULL_HANDLE)
        return scope;
    if (baseDisplayHandler.IsInRendering() and (baseDisplayHandler.m_backBufferCb == cb)) {
        baseDisplayHandler.SuspendBackBuffer();
        scope.backBuffer = true;
        return scope;
    }
    RenderTarget* rt = baseRenderer.GetActiveBuffer();
    if (rt and rt->m_isInRendering and rt->m_cmdList and (rt->m_cmdList->GfxList() == cb)) {
        rt->EndRendering();
        scope.target = rt;
    }
    return scope;
}


void CommandListHandler::ResumeRendering(const RenderingScope& scope) noexcept
{
    if (scope.backBuffer)
        baseDisplayHandler.EnableBackBuffer();
    else if (scope.target)
        scope.target->BeginRendering(false, false);
}


VkCommandBuffer CommandListHandler::UploadCmdBuffer(void) noexcept
{
    if (not m_uploadList) {
        CommandList* cl = CreateCmdList(String("Upload"), true);
        if (not cl)
            return VK_NULL_HANDLE;
        if (not cl->Open(false, true)) {
            m_recycledLists.Push(cl);
            return VK_NULL_HANDLE;
        }
        m_uploadList = cl;
    }
    return m_uploadList->GfxList();
}

// =================================================================================================
// =================================================================================================
// Bind-table state (CPU-side staging of per-draw shader resource bindings).

void CommandListHandler::ResetBindings(void) noexcept
{
    for (uint32_t i = 0; i < kSrvSlots; ++i) {
        m_boundSrvViews[i] = VK_NULL_HANDLE;
        m_boundSrvLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    for (uint32_t i = 0; i < kSamplerSlots; ++i)
        m_boundSamplers[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < kUavSlots; ++i) {
        m_boundStorageBuffers[i] = VK_NULL_HANDLE;
        m_boundStorageBufferSize[i] = 0;
    }
    for (uint32_t i = 0; i < kSsboSlots; ++i) {
        m_boundReadOnlyBuffers[i] = VK_NULL_HANDLE;
        m_boundReadOnlyBufferSize[i] = 0;
    }
    m_boundAccelStructure = VK_NULL_HANDLE;
}


void CommandListHandler::BindAccelerationStructure(VkAccelerationStructureKHR accelStructure) noexcept
{
    m_boundAccelStructure = accelStructure;
}


void CommandListHandler::BindSampledImage(uint32_t slot, VkImageView view, VkImageLayout layout) noexcept
{
    if (slot < kSrvSlots) {
        m_boundSrvViews[slot] = view;
        m_boundSrvLayouts[slot] = layout;
    }
}


void CommandListHandler::BindSampler(uint32_t slot, VkSampler sampler) noexcept
{
    if (slot < kSamplerSlots)
        m_boundSamplers[slot] = sampler;
}


void CommandListHandler::BindStorageBuffer(uint32_t slot, VkBuffer buffer, VkDeviceSize range) noexcept
{
    if (slot < kUavSlots) {
        m_boundStorageBuffers[slot] = buffer;
        m_boundStorageBufferSize[slot] = range;
    }
}


void CommandListHandler::BindReadOnlyBuffer(uint32_t slot, VkBuffer buffer, VkDeviceSize range) noexcept
{
    if (slot < kSsboSlots) {
        m_boundReadOnlyBuffers[slot] = buffer;
        m_boundReadOnlyBufferSize[slot] = range;
    }
}


void CommandListHandler::UnbindBuffer(VkBuffer buffer) noexcept
{
    if (buffer == VK_NULL_HANDLE)
        return;
    for (uint32_t i = 0; i < kUavSlots; ++i)
        if (m_boundStorageBuffers[i] == buffer)
            BindStorageBuffer(i, VK_NULL_HANDLE, 0);
    for (uint32_t i = 0; i < kSsboSlots; ++i)
        if (m_boundReadOnlyBuffers[i] == buffer)
            BindReadOnlyBuffer(i, VK_NULL_HANDLE, 0);
}

// =================================================================================================