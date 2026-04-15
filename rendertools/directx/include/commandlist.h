#pragma once

#include "framework.h"
#include "basesingleton.hpp"
#include "array.hpp"

// =================================================================================================
// Frame protocol:
//   CommandListHandler::ExecuteAll()  — submit all registered lists
//   CommandQueue::EndFrame()          — signal fence, advance frame index
//   CommandQueue::BeginFrame()        — wait for frame slot to be free, reset CBV allocator

class CommandQueue
{
public:
    static constexpr UINT FRAME_COUNT = 2;

    ComPtr<ID3D12CommandQueue>  m_queue;
    ComPtr<ID3D12Fence>         m_fence;
    UINT64                      m_fenceValues[FRAME_COUNT]{};
    UINT64                      m_fenceCounter{ 0 };
    HANDLE                      m_fenceEvent{ nullptr };
    UINT                        m_frameIndex{ 0 };

    bool Create(ID3D12Device* device) noexcept;
    void Destroy(void) noexcept;

    // Waits for the GPU to finish with the current frame slot, then resets the CBV allocator.
    bool BeginFrame(void) noexcept;

    // Signals the fence with the next value and advances the frame index.
    // Call after CommandListHandler::ExecuteAll().
    void EndFrame(void) noexcept;

    // Signals a new fence value and waits until the GPU has passed it.
    void WaitIdle(void) noexcept;

    inline ID3D12CommandQueue* Queue(void) const noexcept { 
        return m_queue.Get(); 
    }
    
    inline UINT FrameIndex(void) const noexcept { 
        return m_frameIndex; 
    }

    // Returns the currently active command list (set by CommandList::Open via PushList).
    // All rendering code calls this to get the right list regardless of which object owns it.
    ID3D12GraphicsCommandList* List(void) const noexcept;
};

// =================================================================================================
// CommandList: wraps a DX12 command list and its per-frame allocators.
//
// Tracks recording state. List() returns nullptr when not recording, which makes all
// downstream guards (if (not list) return;) work correctly without extra checks.
//
// Open()  — reset allocator + list, begin recording, push onto CommandListHandler stack
//            so cmdQueue.List() returns this list for the duration of recording.
// Close() — close list, pop stack, register for frame-end submission via CommandListHandler.
// Flush() — close list, pop stack, submit immediately + WaitIdle, no registration.
//            Used for one-shot setup (FBO::Create, resource uploads).

class CommandList
{
public:
    static constexpr UINT FRAME_COUNT = 2;

    ComPtr<ID3D12GraphicsCommandList>  m_list;
    ComPtr<ID3D12CommandAllocator>     m_allocators[FRAME_COUNT];
    bool                               m_isRecording{ false };

    bool Create(ID3D12Device* device) noexcept;

    void Destroy(void) noexcept;

    bool Open(UINT frameIndex) noexcept;

    void Close(void) noexcept;

    void Flush(void) noexcept;

    inline ID3D12GraphicsCommandList* List(void) const noexcept {
        return m_isRecording ? m_list.Get() : nullptr;
    }

#ifdef _DEBUG
    void CheckDeviceRemoved(const char* context) noexcept;
#endif
};

// =================================================================================================
// CommandListHandler: singleton that routes command lists and drives frame-end submission.
//
// PushList / PopList — called by CommandList::Open / Close / Flush to maintain the active-list
//   stack. cmdQueue.List() always returns the top of this stack.
// Register — called by CommandList::Close to enqueue the closed list for submission.
// ExecuteAll — submits all registered lists in registration order, then clears the queue.
//   Call this once per frame before CommandQueue::EndFrame().

class CommandListHandler
    : public BaseSingleton<CommandListHandler>
{
public:
    CommandQueue                            m_cmdQueue;
    CommandList                             m_uploadCmdList;   // dedicated list for one-shot uploads
    AutoArray<ID3D12GraphicsCommandList*>   m_pendingLists;
    AutoArray<ID3D12GraphicsCommandList*>   m_listStack;
    ID3D12GraphicsCommandList*              m_currentList{ nullptr };

    bool Create(ID3D12Device* device) noexcept;

    void Destroy(void) noexcept;

    inline ID3D12CommandQueue* GetQueue(void) noexcept {
        return m_cmdQueue.Queue(); 
    }

    inline CommandQueue& CmdQueue(void) noexcept {
        return m_cmdQueue;
    }

    // Returns the currently recording list (top of stack), or nullptr if none is open.
    inline ID3D12GraphicsCommandList* CurrentList(void) const noexcept { return m_currentList; }

    // Stack management — called by CommandList::Open / Close / Flush.
    void PushList(ID3D12GraphicsCommandList* list) noexcept;

    void PopList(void) noexcept;

    // Enqueues a closed list for frame-end submission — called by CommandList::Close.
    void Register(ID3D12GraphicsCommandList* list) noexcept;

    // Submits all registered lists to the GPU queue in registration order, then clears the queue.
    void ExecuteAll(void) noexcept;

    // Returns an open, clean CommandList for one-shot uploads.
    // If already recording (previous upload not flushed), flushes it first.
    // The caller is responsible for calling Flush() on the returned list.
    CommandList* GetOpenClean(void) noexcept;
};

#define commandListHandler CommandListHandler::Instance()

// =================================================================================================
