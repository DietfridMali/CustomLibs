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
    // Phase C: route through CommandListHandler::CurrentCmdBuffer().
    return VK_NULL_HANDLE;
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
// CommandList / CommandListHandler — Phase C.
// The 1:1 DX12 carry-over below is preserved as reference and will be ported when
// the CL/RT lifecycle moves to Vulkan VkCommandBuffer + VkCommandPool semantics.

#if 0

#include "cbv_allocator.h"
#include "dx12framework.h"
#include "dx12context.h"
#include "resource_handler.h"
#include "gfxrenderer.h"

List<RenderStates> CommandList::m_renderStateStack;

void CommandList::PushRenderStates(void) noexcept {
    m_renderStateStack.Push(baseRenderer.RenderStates());
}

void CommandList::PopRenderStates(void) noexcept {
    if (m_renderStateStack.Length() > 0)
        baseRenderer.RenderStates() = m_renderStateStack.Pop();
}

// =================================================================================================
// CommandQueue (DX12 1:1 carry-over)

bool CommandQueue::Create(ID3D12Device* device, const String& name) noexcept {
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue));
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue: CreateCommandQueue failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue: CreateFence failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (not m_fenceEvent) {
        fprintf(stderr, "CommandQueue: CreateEvent failed\n");
        return false;
    }

    if (not cbvAllocator.Create(device))
        return false;
#ifdef _DEBUG
    if (not name.IsEmpty())
        m_queue->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.Length(), (const char*)name);
#endif
    return true;
}


void CommandQueue::Destroy(void) noexcept {
    WaitIdle();
    cbvAllocator.Destroy();
    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}


bool CommandQueue::BeginFrame(void) noexcept {
    // Wait until the GPU has finished using this frame's slot.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        HRESULT hr = m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        if (FAILED(hr)) {
            fprintf(stderr, "CommandQueue::BeginFrame: SetEventOnCompletion failed (hr=0x%08X)\n", (unsigned)hr);
            return false;
        }
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    cbvAllocator.Reset(m_frameIndex);
    gfxResourceHandler.Cleanup();
    return true;
}


void CommandQueue::EndFrame(void) noexcept {
    const UINT64 fenceValue = ++m_fenceCounter;
    m_fenceValues[m_frameIndex] = fenceValue;
    m_queue->Signal(m_fence.Get(), fenceValue);
    m_frameIndex = (m_frameIndex + 1) % FRAME_COUNT;
}


void CommandQueue::WaitIdle(void) noexcept {
    if (not m_queue or not m_fence or not m_fenceEvent)
        return;
    const UINT64 value = ++m_fenceCounter;
    m_fenceValues[m_frameIndex] = value;
    m_queue->Signal(m_fence.Get(), value);
    if (m_fence->GetCompletedValue() < value) {
        m_fence->SetEventOnCompletion(value, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
#ifdef _DEBUG
    HRESULT removed = dx12Context.Device() ? dx12Context.Device()->GetDeviceRemovedReason() : E_FAIL;
    if (FAILED(removed)) {
        fprintf(stderr, "CommandQueue::WaitIdle: device removed after wait (0x%08X)\n", (unsigned)removed);
        gfxStates.CheckError();
        dx12Context.DumpDRED();
        fflush(stderr);
    }
#endif
}


ID3D12GraphicsCommandList* CommandQueue::List(void) const noexcept {
    return commandListHandler.CurrentGfxList();
}

// =================================================================================================

bool CommandList::Create(ID3D12Device* device, const String& name, bool isTemporary) noexcept {
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i]));
        if (FAILED(hr)) {
            fprintf(stderr, "CommandList::Create: CreateCommandAllocator[%u] failed (hr=0x%08X)\n", i, (unsigned)hr);
            return false;
        }
    }
    HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_gfxListPtr));
    if (FAILED(hr)) {
        fprintf(stderr, "CommandList::Create: CreateCommandList failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }
    m_gfxListPtr->Close();
    m_id = commandListHandler.m_cmdListId++;
#ifdef _DEBUG
    if (not name.IsEmpty())
        m_gfxListPtr->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.Length(), (const char*)name);
#endif
    m_name = name;
    m_isTemporary = isTemporary;
    return true;
}


void CommandList::Destroy(void) noexcept {
    m_isRecording = false;
    m_gfxListPtr.Reset();
    for (UINT i = 0; i < FRAME_COUNT; ++i)
        m_allocators[i].Reset();
}


void CommandList::Reset(void) noexcept {
    m_refCounter = 1;
    m_isFlushed = false;
    m_isRecording = false;
}


bool CommandList::Open(bool saveRenderStates) noexcept {
    if (m_isRecording)
        return true;
    UINT frameIndex = commandListHandler.CmdQueue().FrameIndex();
    if (not m_gfxListPtr or not m_allocators[frameIndex])
        return false;
    HRESULT hr = m_allocators[frameIndex]->Reset();
    if (FAILED(hr)) {
        fprintf(stderr, "CommandList::Open: allocator Reset failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }
    hr = m_gfxListPtr->Reset(m_allocators[frameIndex].Get(), nullptr);
    if (FAILED(hr)) {
        fprintf(stderr, "CommandList::Open: list Reset failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }
    m_isRecording = true;
    m_isFlushed = false;
    m_activePSO = nullptr;
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


void CommandList::Close(bool restoreRenderStates) noexcept {
    if (not m_isRecording)
        return;
    m_isRecording = false;
    HRESULT hr = m_gfxListPtr->Close();
    if (FAILED(hr))
        fprintf(stderr, "CommandList::Close: list->Close() failed (hr=0x%08X)\n", (unsigned)hr);
#ifdef _DEBUG
    gfxStates.CheckError();
#endif
    commandListHandler.PopCmdList();
    if (restoreRenderStates)
        PopRenderStates();
}


void CommandList::Flush(void) noexcept {
    if (m_isFlushed)
        return;
    m_isFlushed = true;
    Close();

    ID3D12CommandList* lists[] = { GfxList(true) };
    commandListHandler.GetQueue()->ExecuteCommandLists(1, lists);

#ifdef _DEBUG
    CheckDeviceRemoved("Flush");
#endif
    commandListHandler.CmdQueue().WaitIdle();
    DisposeResources();
#if 0
    // Reset allocator + list so the debug layer releases resource refs; leave closed.
    UINT fi = commandListHandler.CmdQueue().FrameIndex();
    HRESULT hr = m_allocators[fi]->Reset();
    if (FAILED(hr))
        fprintf(stderr, "CommandList::Flush: allocator Reset failed (hr=0x%08X)\n", (unsigned)hr);
    hr = m_gfxListPtr->Reset(m_allocators[fi].Get(), nullptr);
    if (FAILED(hr))
        fprintf(stderr, "CommandList::Flush: list Reset failed (hr=0x%08X)\n", (unsigned)hr);
    else
        m_gfxListPtr->Close();
#endif
}


void CommandList::SetBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (not m_isRecording or not resource or (before == after))
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_gfxListPtr->ResourceBarrier(1, &b);
#ifdef _DEBUG
    gfxStates.CheckError();
#endif
}


void CommandList::SetBarrier(D3D12_RESOURCE_BARRIER* barriers, int count) {
    if (not m_isRecording or not barriers or (count <= 0))
        return;
    m_gfxListPtr->ResourceBarrier(UINT(count), barriers);
#ifdef _DEBUG
    gfxStates.CheckError();
#endif
}


void CommandList::DisposeResources(void) noexcept {
    for (auto& fn : m_disposableResources)
        fn();
    m_disposableResources.Clear();
}


void CommandList::SetActivePSO(ID3D12PipelineState* pso, Shader* shader) noexcept {
    if (pso != m_activePSO) {
        m_gfxListPtr->SetPipelineState(pso);
        m_gfxListPtr->SetGraphicsRootSignature(shader->GetRootSignature().Get());
        m_activePSO = pso;
#ifdef _DEBUG
        gfxStates.CheckError();
#endif
    }
}


ID3D12PipelineState* CommandList::GetPSO(Shader* shader) noexcept {
    ID3D12PipelineState* pso = PSO::GetPSO(shader);
    if (pso)
        SetActivePSO(pso, shader);
    return pso;
}


#ifdef _DEBUG
void CommandList::CheckDeviceRemoved(const char* context) noexcept {
    gfxStates.CheckError();
    HRESULT removed = dx12Context.Device() ? dx12Context.Device()->GetDeviceRemovedReason() : E_FAIL;
    if (FAILED(removed)) {
        fprintf(stderr, "CommandList::%s: device removed (0x%08X)\n", context, (unsigned)removed);
        dx12Context.DumpDRED();
        fflush(stderr);
    }
}
#endif

// =================================================================================================
// CommandListHandler

#ifdef _DEBUG
bool CommandListHandler::s_logCalls = false;
#endif

bool CommandListHandler::Create(ID3D12Device* device) noexcept {
    if (not m_cmdQueue.Create(device, "MainQueue"))
        return false;
    return true;
}


void CommandListHandler::Destroy(void) noexcept {
    for (auto cl : m_recycledLists) {
        cl->Destroy();
        delete cl;
    }
    m_recycledLists.Clear();
    m_cmdQueue.Destroy();
}


void CommandListHandler::PushCmdList(CommandList* cl) noexcept {
    if (m_currentListData.cmdList)
        m_cmdListStack.Push(m_currentListData);
    m_currentListData = CommandListData{ cl, cl->GfxList() };
}


void CommandListHandler::PopCmdList(void) noexcept {
    m_currentListData = (m_cmdListStack.Length() > 0) ? m_cmdListStack.Pop() : CommandListData();
}


void CommandListHandler::Register(CommandList* cl) noexcept {
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

#define LOG_EXECUTION 0

void CommandListHandler::ExecuteAll(void) noexcept {
    if (m_pendingLists.IsEmpty())
        return;
	AutoArray< ID3D12CommandList*> execList(m_pendingLists.Length());
    int n = 0;
#if LOG_EXECUTION//def _DEBUG
    fprintf(stderr, "\nCommandListHandler::ExecuteAll: executing %u command lists.\n", (unsigned)m_pendingLists.Length());
#endif
    for (auto l : m_pendingLists) {
#if LOG_EXECUTION//def _DEBUG
        if (l->m_isRecording) {
            fprintf(stderr, "   '%s' still open; closing it now.\n", (const char*)l->GetName());
            l->Close();
        }
        fprintf(stderr, "   executing CommandList '%s' (CL:%p, list:%p).\n", (const char*)l->GetName(), (void*)l, (void*)l->GfxList());
#endif
        if (not l->IsFlushed())
            execList[n++] = l->GfxList(true);
    }
#if LOG_EXECUTION//def _DEBUG
    fprintf(stderr, "\n");
#endif
    if (n > 0)
        m_cmdQueue.Queue()->ExecuteCommandLists(UINT(n), execList.Data());
#ifdef _DEBUG
    gfxStates.CheckError();
    HRESULT removed = dx12Context.Device() ? dx12Context.Device()->GetDeviceRemovedReason() : E_FAIL;
    if (FAILED(removed)) {
        fprintf(stderr, "CommandListHandler::ExecuteAll: device removed (0x%08X)\n", (unsigned)removed);
        dx12Context.DumpDRED();
    }
    fflush(stderr);
#endif
    for (auto l : m_pendingLists) {
        if (l->m_isTemporary)
            m_recycledLists.Push(l);
    }
    m_pendingLists.Clear();
    m_cmdListStack.Clear();
}


CommandList* CommandListHandler::CreateCmdList(const String& name, bool isTemporary) noexcept {
    if (isTemporary and not m_recycledLists.IsEmpty()) {
        CommandList* cl = m_recycledLists.Pop();
        cl->SetName(name);
        cl->Reset();
        return cl;
    }
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return nullptr;

    CommandList* cl = new CommandList();
    if (not cl->Create(device, name, isTemporary)) {
        delete cl;
        return nullptr;
    }
    cl->Reset();
    ++m_cmdListCount;
    return cl;
}

#endif // Phase C

// =================================================================================================
