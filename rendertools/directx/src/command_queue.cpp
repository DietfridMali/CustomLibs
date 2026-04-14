#include "command_queue.h"
#include "cbv_allocator.h"

#include <cstdio>

// =================================================================================================

bool CommandQueue::Create(ID3D12Device* device) noexcept {
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue));
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue: CreateCommandQueue failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i]));
        if (FAILED(hr)) {
            fprintf(stderr, "CommandQueue: CreateCommandAllocator[%u] failed (hr=0x%08X)\n", i, (unsigned)hr);
            return false;
        }
    }

    // Command list starts in recording state; close it immediately so BeginFrame can Reset() it.
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_list));
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue: CreateCommandList failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }
    m_list->Close();

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue: CreateFence failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        fprintf(stderr, "CommandQueue: CreateEvent failed\n");
        return false;
    }

    if (not cbvAllocator.Create(device))
        return false;

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
    // Wait until the GPU has finished using this frame's allocator.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        HRESULT hr = m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        if (FAILED(hr)) {
            fprintf(stderr, "CommandQueue::BeginFrame: SetEventOnCompletion failed (hr=0x%08X)\n", (unsigned)hr);
            return false;
        }
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    HRESULT hr = m_allocators[m_frameIndex]->Reset();
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue::BeginFrame: allocator Reset failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    hr = m_list->Reset(m_allocators[m_frameIndex].Get(), nullptr);
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue::BeginFrame: list Reset failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    cbvAllocator.Reset(m_frameIndex);
    m_isRecording = true;
    return true;
}


bool CommandQueue::Open(void) noexcept {
    if (m_isRecording)
        return true;
    // Wait for GPU to finish with this frame's allocator before resetting it.
    if (m_fence and m_fenceEvent and (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])) {
        HRESULT hr = m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        if (FAILED(hr)) {
            fprintf(stderr, "CommandQueue::Open: SetEventOnCompletion failed (hr=0x%08X)\n", (unsigned)hr);
            return false;
        }
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    HRESULT hr = m_allocators[m_frameIndex]->Reset();
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue::Open: allocator Reset failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }
    hr = m_list->Reset(m_allocators[m_frameIndex].Get(), nullptr);
    if (FAILED(hr)) {
        fprintf(stderr, "CommandQueue::Open: list Reset failed (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    cbvAllocator.Reset(m_frameIndex);
    m_isRecording = true;
    return true;
}


void CommandQueue::Execute(void) noexcept {
    if (!m_isRecording) return;
    m_isRecording = false;
    HRESULT hr = m_list->Close();
    if (FAILED(hr))
        fprintf(stderr, "CommandQueue::Execute: list->Close() failed (hr=0x%08X)\n", (unsigned)hr);
    ID3D12CommandList* lists[] = { m_list.Get() };
    m_queue->ExecuteCommandLists(1, lists);
}


void CommandQueue::EndFrame(void) noexcept {
    Execute();
    const UINT64 fenceValue = ++m_fenceCounter;   // always increasing across all frames
    m_fenceValues[m_frameIndex] = fenceValue;      // store so BeginFrame can wait on it
    m_queue->Signal(m_fence.Get(), fenceValue);
    m_frameIndex = (m_frameIndex + 1) % FRAME_COUNT;
}


void CommandQueue::WaitIdle(void) noexcept {
    if (!m_queue || !m_fence || !m_fenceEvent)
        return;
    const UINT64 value = ++m_fenceCounter;
    m_fenceValues[m_frameIndex] = value;
    m_queue->Signal(m_fence.Get(), value);
    if (m_fence->GetCompletedValue() < value) {
        m_fence->SetEventOnCompletion(value, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

// =================================================================================================
