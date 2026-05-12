#include "vkframework.h"
#include "commandlist.h"
#include "descriptor_pool_handler.h"
#include "cbv_allocator.h"
#include "resource_handler.h"

#include <cstdio>

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
    // Wait until the GPU has finished using this frame slot.
    VkResult res = vkWaitForFences(m_device, 1, &m_inFlight[m_frameIndex], VK_TRUE, UINT64_MAX);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "CommandQueue::BeginFrame: vkWaitForFences failed (%d)\n", (int)res);
        return false;
    }
    res = vkResetFences(m_device, 1, &m_inFlight[m_frameIndex]);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "CommandQueue::BeginFrame: vkResetFences failed (%d)\n", (int)res);
        return false;
    }
    if (not AcquireNextImage())
        return false;
    gfxResourceHandler.Cleanup(m_frameIndex);
    descriptorPoolHandler.BeginFrame(m_frameIndex);
    cbvAllocator.Reset(m_frameIndex);
    return true;
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
    VkResult res = vkQueueWaitIdle(m_graphicsQueue);
    if (res != VK_SUCCESS)
        fprintf(stderr, "CommandQueue::WaitIdle: vkQueueWaitIdle failed (%d)\n", (int)res);
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
        VkResult r2 = vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinished[i]);
        VkResult r3 = vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlight[i]);
        if ((r1 != VK_SUCCESS) or (r2 != VK_SUCCESS) or (r3 != VK_SUCCESS)) {
            fprintf(stderr, "CommandQueue::CreateSyncObjects: failed at slot %u (sem=%d,%d fence=%d)\n",
                    i, (int)r1, (int)r2, (int)r3);
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
        if (m_renderFinished[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, m_renderFinished[i], nullptr);
            m_renderFinished[i] = VK_NULL_HANDLE;
        }
        if (m_inFlight[i] != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, m_inFlight[i], nullptr);
            m_inFlight[i] = VK_NULL_HANDLE;
        }
    }
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
        return false;
    }
    return true;
}


void CommandQueue::Present(void) noexcept
{
    VkPresentInfoKHR present { };
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_renderFinished[m_frameIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &m_imageIndex;

    VkResult res = vkQueuePresentKHR(m_presentQueue, &present);
    if ((res != VK_SUCCESS) and (res != VK_SUBOPTIMAL_KHR) and (res != VK_ERROR_OUT_OF_DATE_KHR))
        fprintf(stderr, "CommandQueue::Present: vkQueuePresentKHR failed (%d)\n", (int)res);
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
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = vkContext.GraphicsFamily();
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        VkResult res = vkCreateCommandPool(device, &poolInfo, nullptr, &m_pools[i]);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "CommandList::Create: vkCreateCommandPool[%u] failed (%d)\n", i, (int)res);
            return false;
        }
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_pools[i];
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
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


bool CommandList::Open(bool saveRenderStates) noexcept
{
    if (m_isRecording)
        return true;
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
    m_activePipeline = VK_NULL_HANDLE;
    ++m_executionCounter;
    commandListHandler.PushCmdList(this);
    commandListHandler.Register(this);
    if (saveRenderStates)
        PushRenderStates();
#ifdef _DEBUG
    gfxStates.CheckError();
#endif
    return true;
}


void CommandList::Close(bool restoreRenderStates) noexcept
{
    if (not m_isRecording)
        return;
    m_isRecording = false;
    uint32_t fi = ActiveFrameIndex();
    VkResult res = vkEndCommandBuffer(m_cmdBuffers[fi]);
    if (res != VK_SUCCESS)
        fprintf(stderr, "CommandList::Close: vkEndCommandBuffer failed (%d)\n", (int)res);
#ifdef _DEBUG
    gfxStates.CheckError();
#endif
    commandListHandler.PopCmdList();
    if (restoreRenderStates)
        PopRenderStates();
}


void CommandList::Flush(void) noexcept
{
    if (m_isFlushed)
        return;
    m_isFlushed = true;
    Close();

    uint32_t fi = ActiveFrameIndex();
    VkCommandBufferSubmitInfo cbInfo{};
    cbInfo.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbInfo.commandBuffer = m_cmdBuffers[fi];

    VkSubmitInfo2 submit{};
    submit.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos    = &cbInfo;

    VkResult res = vkQueueSubmit2(commandListHandler.GetQueue(), 1, &submit, VK_NULL_HANDLE);
    if (res != VK_SUCCESS)
        fprintf(stderr, "CommandList::Flush: vkQueueSubmit2 failed (%d)\n", (int)res);
#ifdef _DEBUG
    CheckDeviceRemoved("Flush");
#endif
    commandListHandler.CmdQueue().WaitIdle();
    DisposeResources();
}


void CommandList::SetBarrier(VkImage image, ImageLayoutTracker& tracker, VkImageLayout newLayout,
                             VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    if (not m_isRecording or (image == VK_NULL_HANDLE))
        return;
    tracker.TransitionTo(GfxList(), newLayout, dstStage, dstAccess);
#ifdef _DEBUG
    gfxStates.CheckError();
#endif
}


void CommandList::SetBarrier(const VkImageMemoryBarrier2* barriers, int count)
{
    if (not m_isRecording or not barriers or (count <= 0))
        return;
    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = uint32_t(count);
    dep.pImageMemoryBarriers    = barriers;
    vkCmdPipelineBarrier2(GfxList(), &dep);
#ifdef _DEBUG
    gfxStates.CheckError();
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
        gfxStates.CheckError();
#endif
    }
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
    if (p != VK_NULL_HANDLE)
        SetActivePipeline(p, shader);
    return p;
}


#ifdef _DEBUG
void CommandList::CheckDeviceRemoved(const char* context) noexcept
{
    int errors = vkContext.DrainMessages(false);
    if (errors > 0) {
        fprintf(stderr, "CommandList::%s: %d Vulkan validation error(s)\n", context, errors);
        fflush(stderr);
    }
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
    return m_cmdQueue.Create(device, graphicsQueue, presentQueue, graphicsFamily, presentFamily, name);
}


void CommandListHandler::Destroy(void) noexcept
{
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
    // intermediate=false (default): frame-end submit — binds the swapchain frame-sync triplet
    //   (imageAvailable wait, renderFinished signal, inFlight fence). Must be called between a
    //   prior BeginFrame (which signaled imageAvailable via vkAcquireNextImageKHR and reset the
    //   inFlight fence) and a subsequent Present (which waits on renderFinished).
    // intermediate=true: setup-phase or mid-init drain — plain submit with no frame-sync objects,
    //   followed by vkQueueWaitIdle. Lets setup-phase CommandLists go to the GPU and finish
    //   before the first BeginFrame resets cbvAllocator / drains gfxResourceHandler.
    if (m_pendingLists.IsEmpty())
        return;

    AutoArray<VkCommandBufferSubmitInfo> cbInfos(m_pendingLists.Length());
    int n = 0;
    for (auto l : m_pendingLists) {
        if (l->IsFlushed())
            continue;
        if (l->IsRecording())
            l->Close();
        VkCommandBufferSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        info.commandBuffer = l->GfxList(true);
        cbInfos[n++] = info;
    }
    if (n > 0) {
        VkSubmitInfo2 submit{};
        submit.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.commandBufferInfoCount = uint32_t(n);
        submit.pCommandBufferInfos    = cbInfos.Data();

        VkSemaphoreSubmitInfo waitInfo{};
        VkSemaphoreSubmitInfo signalInfo{};
        VkFence fence = VK_NULL_HANDLE;
        if (not intermediate) {
            waitInfo.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfo.semaphore = m_cmdQueue.SubmitWaitSemaphore();
            waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            signalInfo.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfo.semaphore = m_cmdQueue.SubmitSignalSemaphore();
            signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            submit.waitSemaphoreInfoCount   = 1;
            submit.pWaitSemaphoreInfos      = &waitInfo;
            submit.signalSemaphoreInfoCount = 1;
            submit.pSignalSemaphoreInfos    = &signalInfo;
            fence = m_cmdQueue.SubmitSignalFence();
        }

        VkResult res = vkQueueSubmit2(m_cmdQueue.GraphicsQueue(), 1, &submit, fence);
        if (res != VK_SUCCESS)
            fprintf(stderr, "CommandListHandler::ExecuteAll: vkQueueSubmit2 failed (%d)\n", (int)res);
    }
#ifdef _DEBUG
    gfxStates.CheckError();
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

// =================================================================================================
// =================================================================================================
// Bind-table state (CPU-side staging of per-draw shader resource bindings).

void CommandListHandler::ResetBindings(void) noexcept
{
    for (uint32_t i = 0; i < kSrvSlots; ++i)
        m_boundSrvViews[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < kSamplerSlots; ++i)
        m_boundSamplers[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < kUavSlots; ++i)
        m_boundStorageViews[i] = VK_NULL_HANDLE;
}


void CommandListHandler::BindSampledImage(uint32_t slot, VkImageView view) noexcept
{
    if (slot < kSrvSlots)
        m_boundSrvViews[slot] = view;
}


void CommandListHandler::BindSampler(uint32_t slot, VkSampler sampler) noexcept
{
    if (slot < kSamplerSlots)
        m_boundSamplers[slot] = sampler;
}


void CommandListHandler::BindStorageImage(uint32_t slot, VkImageView view) noexcept
{
    if (slot < kUavSlots)
        m_boundStorageViews[slot] = view;
}

// =================================================================================================