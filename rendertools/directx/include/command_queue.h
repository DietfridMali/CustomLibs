#pragma once

#include "framework.h"
#include "basesingleton.hpp"

// =================================================================================================
// CommandQueue: wraps the DX12 direct command queue with double-buffered command allocators.
// Each frame, one allocator is in use (GPU); the other is being reset and recorded into (CPU).
// The Fence + Win32 event guarantees the CPU waits before reusing a frame's allocator.

class CommandQueue
{
public:
    static constexpr UINT FRAME_COUNT = 2;

    ComPtr<ID3D12CommandQueue>          m_queue;
    ComPtr<ID3D12CommandAllocator>      m_allocators[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList>   m_list;
    ComPtr<ID3D12Fence>                 m_fence;
    UINT64                              m_fenceValues[FRAME_COUNT]{};
    HANDLE                              m_fenceEvent{ nullptr };
    UINT                                m_frameIndex{ 0 };

    // Creates the queue, two command allocators, the command list (initially closed) and the fence.
    bool Create(ID3D12Device* device) noexcept;

    // Releases all resources; calls WaitIdle() first.
    void Destroy(void) noexcept;

    // Waits for the current frame's allocator to be free, resets it and the command list.
    // Must be called at the start of every frame before recording commands.
    bool BeginFrame(void) noexcept;

    // Closes the command list, executes it, signals the fence, then advances the frame index.
    // Must be called after all commands for the current frame have been recorded.
    void EndFrame(void) noexcept;

    // Flush: signals the fence with the next value and waits for it on the CPU.
    // Safe to call at any time (e.g. before resizing swap chain, on shutdown).
    void WaitIdle(void) noexcept;

    // Closes and submits the current command list without advancing the frame index.
    // Called internally by EndFrame; can also be used for mid-frame flushes.
    void Execute(void) noexcept;

    inline ID3D12CommandQueue*          Queue(void) const noexcept { return m_queue.Get(); }
    inline ID3D12GraphicsCommandList*   List(void)  const noexcept { return m_list.Get(); }
    inline UINT                         FrameIndex(void) const noexcept { return m_frameIndex; }
};

// =================================================================================================

class CommandQueueHandler : public BaseSingleton<CommandQueueHandler>
{
public:
    CommandQueue m_cmdQueue;

    bool Create(ID3D12Device* device) noexcept {
        return m_cmdQueue.Create(device);
    }

    void Destroy(void) noexcept {
        m_cmdQueue.Destroy();
    }

    inline CommandQueue& Get(void) noexcept { return m_cmdQueue; }
};

#define cmdQueue CommandQueueHandler::Instance().Get()

// =================================================================================================
