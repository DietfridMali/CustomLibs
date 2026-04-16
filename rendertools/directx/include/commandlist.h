#pragma once

#include "framework.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "string.hpp"
#include <functional>

#ifdef _DEBUG
#include <source_location>
#include <cstdio>
#endif

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

    bool Create(ID3D12Device* device, const String& name = "") noexcept;

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
//            Used for one-shot setup (RenderTarget::Create, resource uploads).

class CommandList
{
public:
    static constexpr UINT FRAME_COUNT = 2;

    ComPtr<ID3D12GraphicsCommandList>   m_list;
    ComPtr<ID3D12CommandAllocator>      m_allocators[FRAME_COUNT];
    bool                                m_isRecording{ false };
    AutoArray<std::function<void()>>    m_disposableResources;
    uint64_t                            m_id{ 0 };           // unique ID assigned once at Create (by CommandListHandler)
    uint64_t                            m_executionCounter{ 0 };  // increments on each Open()
    String                              m_name{ "" };

    bool Create(ID3D12Device* device, const String& name = "") noexcept;

    void Destroy(void) noexcept;

    bool Open(UINT frameIndex) noexcept;

    void Close(void) noexcept;

    void Flush(void) noexcept;

    // Registers a callback to be invoked after this list's GPU work has completed.
    // Use for resources that must outlive recording but can be freed after execution.
    inline void AddResource(std::function<void()> fn) {
        m_disposableResources.Append(std::move(fn));
    }

    void DisposeResources(void) noexcept;

    inline ID3D12GraphicsCommandList* List(void) const noexcept {
        return m_isRecording ? m_list.Get() : nullptr;
    }

	inline uint64_t GetId(void) const noexcept {
		return m_id;
	}

	inline String GetName(void) const noexcept {
		return m_name;
	}

    inline uint64_t GetExecutionCounter(void) const noexcept {
		return m_executionCounter;
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
    AutoArray<CommandList*>                 m_pendingLists;    // registered at Open(), cleared after ExecuteAll
    AutoArray<ID3D12GraphicsCommandList*>   m_listStack;
    AutoArray<CommandList*>                 m_cmdListObjStack;
    ID3D12GraphicsCommandList*              m_currentList{ nullptr };
    CommandList*                            m_currentCmdList{ nullptr };
    uint64_t                                m_cmdListId{ 1 };

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

    // Returns the currently recording CommandList object (top of obj-stack), or nullptr.
    inline CommandList* GetCurrentCmdListObj(void) const noexcept { return m_currentCmdList; }

    // Stack management — called by CommandList::Open / Close / Flush.
    void PushList(ID3D12GraphicsCommandList* list) noexcept;

    void PopList(void) noexcept;

    void PushCmdList(CommandList* cl) noexcept;

    void PopCmdList(void) noexcept;

    // Called by CommandList::Open — registers the list for frame-end submission tracking.
    // m_isRecording distinguishes open (true) from closed/ready (false) at ExecuteAll time.
    void Register(CommandList* cl) noexcept;

    // Submits all closed lists (m_isRecording == false) to the GPU queue in registration order.
    // In debug builds, warns about any CommandLists still open (m_isRecording == true).
    // Clears m_pendingLists afterwards.
    void ExecuteAll(void) noexcept;

    // Allocates a new CommandList on the heap, assigns it a unique m_id, calls Create().
    // Returns the pointer on success, nullptr on failure. Caller owns the memory.
    CommandList* CreateCmdList(const String& name = "") noexcept;

    // Returns an open, clean CommandList for one-shot uploads.
    // If already recording (previous upload not flushed), flushes it first.
    // The caller is responsible for calling Flush() on the returned list.
    CommandList* GetOpenClean(void) noexcept;

#ifdef _DEBUG
    // Set to true to print every logged GPU call (DrawInstanced etc.) with file/line to stderr.
    // Defaults to false to avoid flooding the output in normal operation.
    static bool s_logCalls;

    inline void DrawInstanced(UINT vtxCount, UINT instCount, UINT startVtx, UINT startInst, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[DI]  %u x%u  %s:%u\n", vtxCount, instCount, loc.file_name(), loc.line());
        if (m_currentList)
            m_currentList->DrawInstanced(vtxCount, instCount, startVtx, startInst);
    }

    inline void DrawIndexedInstanced(UINT idxCount, UINT instCount, UINT startIdx, INT baseVtx, UINT startInst, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[DII] %u x%u  %s:%u\n", idxCount, instCount, loc.file_name(), loc.line());
        if (m_currentList)
            m_currentList->DrawIndexedInstanced(idxCount, instCount, startIdx, baseVtx, startInst);
    }

    inline void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst, UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION* src, const D3D12_BOX* srcBox,
                                   std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[CTR] %s:%u\n", loc.file_name(), loc.line());
        if (m_currentList)
            m_currentList->CopyTextureRegion(dst, dstX, dstY, dstZ, src, srcBox);
    }

    inline void ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers,
                                 std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[RB]  n=%u  %s:%u\n", numBarriers, loc.file_name(), loc.line());
        if (m_currentList)
            m_currentList->ResourceBarrier(numBarriers, barriers);
    }
#endif
};

#define commandListHandler CommandListHandler::Instance()

// =================================================================================================
