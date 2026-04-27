#pragma once

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "string.hpp"
#include "gfxstates.h"
//#include "shader.h"
#include "dx12framework.h"
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
//            Used for temporary setup (RenderTarget::Create, resource uploads).

class CommandList
{
public:
    static constexpr UINT FRAME_COUNT = 2;

    ComPtr<ID3D12GraphicsCommandList>   m_gfxListPtr{ nullptr };
    ComPtr<ID3D12CommandAllocator>      m_allocators[FRAME_COUNT];
    bool                                m_isRecording{ false };
    bool                                m_isFlushed{ false };
    bool                                m_isTemporary{ false };
    AutoArray<std::function<void()>>    m_disposableResources;
    uint64_t                            m_id{ 0 };           // unique ID assigned once at Create (by CommandListHandler)
    uint64_t                            m_executionCounter{ 0 };  // increments on each Open()
    uint32_t                            m_refCounter{ 1 };
    String                              m_name{ "" };
    ID3D12PipelineState*                m_activePSO{ nullptr };

    static List<RenderStates>           m_renderStateStack;

    static void PushRenderStates(void) noexcept;

    static void PopRenderStates(void) noexcept;

    bool Create(ID3D12Device* device, const String& name = "", bool isTemporary = false) noexcept;

    void Destroy(void) noexcept;

    void Reset(void) noexcept;

    bool Open(UINT frameIndex = 0) noexcept;

    void Close(void) noexcept;

    void Flush(void) noexcept;

    // Registers a callback to be invoked after this list's GPU work has completed.
    // Use for resources that must outlive recording but can be freed after execution.
    inline void AddResource(std::function<void()> fn) {
        m_disposableResources.Append(std::move(fn));
    }

    void DisposeResources(void) noexcept;

    void SetBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    void SetBarrier(D3D12_RESOURCE_BARRIER* barriers, int count);

    inline ID3D12GraphicsCommandList* GfxList(bool ignoreState = false) const noexcept {
        return (ignoreState or m_isRecording) ? m_gfxListPtr.Get() : nullptr;
    }

	inline uint64_t GetId(void) const noexcept {
		return m_id;
	}

	inline String GetName(void) const noexcept {
		return m_name;
	}

    inline void SetName(String name) noexcept {
        m_name = name;
    }

    inline uint64_t GetExecutionCounter(void) const noexcept {
		return m_executionCounter;
	}

	inline bool IsRecording(void) const noexcept {
		return m_isRecording;
	}

    inline bool IsFlushed(void) const noexcept {
        return m_isFlushed;
    }

	inline bool IsTemporary(void) const noexcept {
		return m_isTemporary;
	}

	inline void SetTemporary(bool value) noexcept {
		m_isTemporary = value;
	}

    void SetActivePSO(ID3D12PipelineState* pso, Shader* shader) noexcept;

    ID3D12PipelineState* GetPSO(Shader* shader) noexcept;

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

struct CommandListData {
    CommandList*                cmdList{ nullptr };
    ID3D12GraphicsCommandList*  gfxList{ nullptr };
};

class CommandListHandler
    : public BaseSingleton<CommandListHandler>
{
public:
    CommandQueue                            m_cmdQueue;
    AutoArray<CommandList*>                 m_pendingLists;     // registered at Open(), cleared after ExecuteAll
    AutoArray<CommandList*>                 m_recycledLists;    // pool of temporary CLs available for reuse
    AutoArray<CommandListData>              m_cmdListStack;
    CommandListData                         m_currentListData;
    uint64_t                                m_cmdListId{ 1 };
    uint64_t                                m_cmdListCount{ 0 };

    bool Create(ID3D12Device* device) noexcept;

    void Destroy(void) noexcept;

    inline ID3D12CommandQueue* GetQueue(void) noexcept {
        return m_cmdQueue.Queue(); 
    }

    inline CommandQueue& CmdQueue(void) noexcept {
        return m_cmdQueue;
    }

    // Returns the currently recording list (top of stack), or nullptr if none is open.
    inline ID3D12GraphicsCommandList* CurrentGfxList(void) const noexcept { 
        return m_currentListData.gfxList; 
    }

    // Returns the currently recording CommandList object (top of obj-stack), or nullptr.
    inline CommandList* CurrentCmdList(void) const noexcept { 
        return m_currentListData.cmdList; 
    }

    void PushCmdList(CommandList* cl) noexcept;

    void PopCmdList(void) noexcept;

    // Called by CommandList::Open — registers the list for frame-end submission tracking.
    // m_isRecording distinguishes open (true) from closed/ready (false) at ExecuteAll time.
    void Register(CommandList* cl) noexcept;

    // Submits all closed lists (m_isRecording == false) to the GPU queue in registration order.
    // In debug builds, warns about any CommandLists still open (m_isRecording == true).
    // Clears m_pendingLists afterwards.
    void ExecuteAll(void) noexcept;

    // Returns a CommandList. If isTemporary is true, tries to reuse one from m_recycledLists
    // before allocating a new one. Temporary CLs are recycled after ExecuteAll().
    // Caller owns the memory for non-temporary lists.
#ifdef _DEBUG
    // Set to true to print every logged GPU call (DrawInstanced etc.) with file/line to stderr.
    // Defaults to false to avoid flooding the output in normal operation.
    static bool s_logCalls;

    inline void DrawInstanced(UINT vtxCount, UINT instCount, UINT startVtx, UINT startInst, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[DI]  %u x%u  %s:%u\n", vtxCount, instCount, loc.file_name(), loc.line());
        if (CurrentGfxList())
            CurrentGfxList()->DrawInstanced(vtxCount, instCount, startVtx, startInst);
    }

    inline void DrawIndexedInstanced(UINT idxCount, UINT instCount, UINT startIdx, INT baseVtx, UINT startInst, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[DII] %u x%u  %s:%u\n", idxCount, instCount, loc.file_name(), loc.line());
        if (CurrentGfxList())
            CurrentGfxList()->DrawIndexedInstanced(idxCount, instCount, startIdx, baseVtx, startInst);
    }

    inline void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst, UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION* src, const D3D12_BOX* srcBox, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[CTR] %s:%u\n", loc.file_name(), loc.line());
        if (CurrentGfxList())
            CurrentGfxList()->CopyTextureRegion(dst, dstX, dstY, dstZ, src, srcBox);
    }

    inline void ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[RB]  n=%u  %s:%u\n", numBarriers, loc.file_name(), loc.line());
        if (CurrentGfxList())
            CurrentGfxList()->ResourceBarrier(numBarriers, barriers);
    }
#endif
    CommandList* CreateCmdList(const String& name = "", bool isTemporary = true) noexcept;
};

#define commandListHandler CommandListHandler::Instance()

// =================================================================================================
