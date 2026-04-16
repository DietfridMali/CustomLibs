#include "cbv_allocator.h"
#include "dx12context.h"
#include "commandlist.h"

#include <cstdio>

// =================================================================================================
// CommandQueue

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

    if (not name.IsEmpty())
        m_queue->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.Length(), (const char*)name);

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
        dx12Context.DrainMessages();
        dx12Context.DumpDRED();
        fflush(stderr);
    }
#endif
}


ID3D12GraphicsCommandList* CommandQueue::List(void) const noexcept {
    return commandListHandler.CurrentList();
}

// =================================================================================================

bool CommandList::Create(ID3D12Device* device, const String& name) noexcept {
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i]));
        if (FAILED(hr)) {
            fprintf(stderr, "CommandList::Create: CreateCommandAllocator[%u] failed (hr=0x%08X)\n", i, (unsigned)hr);
            return false;
        }
    }
    HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_list));
    if (FAILED(hr)) {
        fprintf(stderr, "CommandList::Create: CreateCommandList failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }
    m_list->Close();
    m_id = commandListHandler.m_cmdListId++;
    if (not name.IsEmpty())
        m_list->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.Length(), (const char*)name);
    m_name = name;
    return true;
}


void CommandList::Destroy(void) noexcept {
    m_isRecording = false;
    m_list.Reset();
    for (UINT i = 0; i < FRAME_COUNT; ++i)
        m_allocators[i].Reset();
}


bool CommandList::Open(UINT frameIndex) noexcept {
    if (m_isRecording)
        return true;
    if (not m_list or not m_allocators[frameIndex])
        return false;
    HRESULT hr = m_allocators[frameIndex]->Reset();
    if (FAILED(hr)) {
        fprintf(stderr, "CommandList::Open: allocator Reset failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }
    hr = m_list->Reset(m_allocators[frameIndex].Get(), nullptr);
    if (FAILED(hr)) {
        fprintf(stderr, "CommandList::Open: list Reset failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }
    m_isRecording = true;
    ++m_executionCounter;
    commandListHandler.PushList(m_list.Get());
    commandListHandler.PushCmdList(this);
    commandListHandler.Register(this);
    return true;
}


void CommandList::Close(void) noexcept {
    if (not m_isRecording)
        return;
    m_isRecording = false;
    HRESULT hr = m_list->Close();
    if (FAILED(hr))
        fprintf(stderr, "CommandList::Close: list->Close() failed (hr=0x%08X)\n", (unsigned)hr);
    commandListHandler.PopList();
    commandListHandler.PopCmdList();
}


void CommandList::Flush(void) noexcept {
    if (not m_isRecording)
        return;
    m_isRecording = false;
    HRESULT hr = m_list->Close();
    if (FAILED(hr)) {
        fprintf(stderr, "CommandList::Flush: list->Close() failed (hr=0x%08X)\n", (unsigned)hr);
        commandListHandler.PopList();
        return;
    }
    commandListHandler.PopList();
    commandListHandler.PopCmdList();
    ID3D12CommandList* lists[] = { m_list.Get() };
    commandListHandler.GetQueue()->ExecuteCommandLists(1, lists);
#ifdef _DEBUG
    CheckDeviceRemoved("Flush");
#endif
    commandListHandler.CmdQueue().WaitIdle();
    DisposeResources();
    // Reset allocator + list so the debug layer releases resource refs; leave closed.
    UINT fi = commandListHandler.CmdQueue().FrameIndex();
    hr = m_allocators[fi]->Reset();
    if (FAILED(hr))
        fprintf(stderr, "CommandList::Flush: allocator Reset failed (hr=0x%08X)\n", (unsigned)hr);
    hr = m_list->Reset(m_allocators[fi].Get(), nullptr);
    if (FAILED(hr))
        fprintf(stderr, "CommandList::Flush: list Reset failed (hr=0x%08X)\n", (unsigned)hr);
    else
        m_list->Close();
}


void CommandList::DisposeResources(void) noexcept {
    for (auto& fn : m_disposableResources)
        fn();
    m_disposableResources.Clear();
}


#ifdef _DEBUG
void CommandList::CheckDeviceRemoved(const char* context) noexcept {
    dx12Context.DrainMessages();
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
    if (not m_uploadCmdList.Create(device, "upload"))
        return false;
    return true;
}


void CommandListHandler::Destroy(void) noexcept {
    m_uploadCmdList.Destroy();
    m_cmdQueue.Destroy();
}


void CommandListHandler::PushList(ID3D12GraphicsCommandList* list) noexcept {
    if (m_currentList)
        m_listStack.Push(m_currentList);
    m_currentList = list;
}


void CommandListHandler::PopList(void) noexcept {
    m_currentList = (m_listStack.Length() > 0) ? m_listStack.Pop() : nullptr;
}


void CommandListHandler::PushCmdList(CommandList* cl) noexcept {
    if (m_currentCmdList)
        m_cmdListObjStack.Push(m_currentCmdList);
    m_currentCmdList = cl;
}


void CommandListHandler::PopCmdList(void) noexcept {
    m_currentCmdList = (m_cmdListObjStack.Length() > 0) ? m_cmdListObjStack.Pop() : nullptr;
}


void CommandListHandler::Register(CommandList* cl) noexcept {
    if (not cl)
        return;
    for (auto l : m_pendingLists)
        if (cl == l)
            return;
    m_pendingLists.Push(cl);
}



void CommandListHandler::ExecuteAll(void) noexcept {
    if (m_pendingLists.IsEmpty())
        return;
	AutoArray< ID3D12CommandList*> execList(m_pendingLists.Length());
    int n = 0;
    for (auto l : m_pendingLists) {
#ifdef _DEBUG
        if (l->m_isRecording) {
            fprintf(stderr, "CommandListHandler::ExecuteAll: CommandList %p still open — closing\n", static_cast<void*>(l));
            l->Close();
        }
#endif
        execList[n++] = l->m_list.Get();
    }
    if (n > 0)
        m_cmdQueue.Queue()->ExecuteCommandLists(UINT(n), execList.Data());
#ifdef _DEBUG
    dx12Context.DrainMessages();
    HRESULT removed = dx12Context.Device() ? dx12Context.Device()->GetDeviceRemovedReason() : E_FAIL;
    if (FAILED(removed)) {
        fprintf(stderr, "CommandListHandler::ExecuteAll: device removed (0x%08X)\n", (unsigned)removed);
        dx12Context.DumpDRED();
    }
    fflush(stderr);
#endif
    m_pendingLists.Clear();
    m_listStack.Clear();
}


CommandList* CommandListHandler::CreateCmdList(const String& name) noexcept {
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return nullptr;
    CommandList* cl = new CommandList();
    if (not cl->Create(device, name)) {
        delete cl;
        return nullptr;
    }
    return cl;
}


CommandList* CommandListHandler::GetOpenClean(void) noexcept {
    if (m_uploadCmdList.m_isRecording)
        m_uploadCmdList.Flush();
    return m_uploadCmdList.Open(m_cmdQueue.FrameIndex()) ? &m_uploadCmdList : nullptr;
}

// =================================================================================================
