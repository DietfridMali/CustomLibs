#pragma once

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "string.hpp"
#include "gfxstates.h"
#include "tracy_wrapper.h"
#include "dx12framework.h"
#include <functional>

#if DBG_DIRECTX
#include <source_location>
#include <cstdio>
#endif

// =================================================================================================
// Frame protocol (driven by CommandListHandler — single owner of m_frameIndex/m_frameCount):
//   CommandListHandler::BeginFrame(frameIndex) — set or advance index, wait fence, drain slot
//   CommandListHandler::ExecuteAll()           — submit all registered lists
//   CommandListHandler::EndFrame()             — signal fence for current slot
//   CommandListHandler::Flush()                — WaitIdle + drain current slot (init / no-frame path)

class CommandQueue
{
public:
    static constexpr UINT FRAME_COUNT = 2;

    ComPtr<ID3D12CommandQueue>  m_queue;
    ComPtr<ID3D12Fence>         m_fence;
    UINT64                      m_fenceValues[FRAME_COUNT]{};
    UINT64                      m_fenceCounter{ 0 };
    HANDLE                      m_fenceEvent{ nullptr };

    bool Create(ID3D12Device* device, const String& name = "") noexcept;

    void Destroy(void) noexcept;

    // Signals a new fence value and waits until the GPU has passed it. Full CPU/GPU sync.
    void WaitIdle(void) noexcept;

    // Signals the fence for the given slot with the next counter value.
    void Signal(int frameIndex) noexcept;

    // Blocks until the GPU has passed the fence value previously signalled for the given slot.
    void WaitForFrame(int frameIndex) noexcept;

    inline ID3D12CommandQueue* Queue(void) const noexcept {
        return m_queue.Get();
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
    static constexpr uint32_t kSrvSlots     = 16;
    static constexpr uint32_t kSamplerSlots = 16;
    static constexpr uint32_t kUavSlots     = 4;
    static constexpr uint32_t kSsboSlots    = 20;
    static constexpr int      kTableSrv     = 0;
    static constexpr int      kTableUav     = 1;
    static constexpr int      kTableSsbo    = 2;
    static constexpr int      kTableCount   = 3;

    ComPtr<ID3D12GraphicsCommandList>   m_gfxListPtr{ nullptr };
    ComPtr<ID3D12CommandAllocator>      m_allocators[FRAME_COUNT];
    bool                                m_isRecording{ false };
    bool                                m_isFlushed{ false };
    bool                                m_isTemporary{ false };
    bool                                m_savedRenderStates{ false };
    uint64_t                            m_openSerial{ 0 };
    uint64_t                            m_openQueryCount{ 0 };
    AutoArray<std::function<void()>>    m_disposableResources;
    uint64_t                            m_id{ 0 };           // unique ID assigned once at Create (by CommandListHandler)
    uint64_t                            m_executionCounter{ 0 };  // increments on each Open()
    uint32_t                            m_refCounter{ 1 };
    String                              m_name{ "" };
    ID3D12PipelineState*                m_activePSO{ nullptr };
    ID3D12RootSignature*                m_activeRootSignature{ nullptr };
    uint8_t                             m_activeTopology{ uint8_t(MeshTopology::Triangles) };
    bool                                m_descriptorHeapsBound{ false };  // SetDescriptorHeaps issued since this list's Open()
    uint64_t                            m_appliedTables[kTableCount]{};
    uint32_t                            m_appliedSamplers[kSamplerSlots]{};
    tracy::D3D12ZoneScope*              m_gpuZone{ nullptr };   // per-CL GPU profiling zone; spans Open()..Close() (USE_TRACY)

    static List<RenderStates>           m_renderStateStack;

    static void PushRenderStates(void) noexcept;

    static void PopRenderStates(void) noexcept;

    bool Create(ID3D12Device* device, const String& name = "", bool isTemporary = false) noexcept;

    void Destroy(void) noexcept;

    void Reset(void) noexcept;

    bool Open(bool saveRenderStates = true) noexcept;

    void Close(bool restoreRenderStates = true) noexcept;

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
#if DBG_DIRECTX
        if (m_gfxListPtr and not m_name.IsEmpty())
            m_gfxListPtr->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(m_name.Length()), static_cast<const char*>(m_name));
#endif
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

    bool SetTopology(Shader* shader, MeshTopology topology) noexcept;

    // Binds the GPU-visible SRV + sampler descriptor heaps. SetDescriptorHeaps is per-CommandList
    // state and only needs to be issued once after Open() — the flag is cleared there.
    void BindDescriptorHeaps(void) noexcept;

    void ResetAppliedBindings(void) noexcept;

#if DBG_DIRECTX
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
    AutoArray<CommandList*>                 m_recordingLists;
    CommandListData                         m_currentListData;
    uint64_t                                m_cmdListId{ 1 };
    uint64_t                                m_cmdListCount{ 0 };
    int                                     m_frameIndex{ 0 };
    int                                     m_frameCount{ CommandQueue::FRAME_COUNT };
    uint64_t                                m_frameNumber{ 0 };   // monotonic; ++ per BeginFrame — reliable frame-boundary signal
    TracyD3D12Ctx                           m_gpuProfilerCtx{ nullptr };   // Tracy D3D12 GPU-timestamp context; nullptr when USE_TRACY=0
    uint64_t                                m_closedQueryCount{ 0 };

    struct BuiltTable {
        uint64_t                    version{ 0 };
        uint64_t                    generation{ 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{ 0 };
    };

    struct BoundBuffer {
        ComPtr<ID3D12Resource>*     pResource{ nullptr };
        D3D12_RESOURCE_STATES*      pState{ nullptr };
    };

    BoundBuffer                             m_storageBufferStates[CommandList::kUavSlots]{};
    BoundBuffer                             m_readOnlyBufferStates[CommandList::kSsboSlots]{};

    uint32_t                                m_boundSrvs[CommandList::kSrvSlots]{};
    uint32_t                                m_boundSamplers[CommandList::kSamplerSlots]{};
    uint32_t                                m_boundStorageBuffers[CommandList::kUavSlots]{};
    uint32_t                                m_boundReadOnlyBuffers[CommandList::kSsboSlots]{};
    uint64_t                                m_bindingVersions[CommandList::kTableCount]{};
    uint64_t                                m_bindingVersionCounter{ 0 };
    uint64_t                                m_srvDefaultKey{ 0 };
    BuiltTable                              m_builtTables[CommandList::kTableCount];

    bool Create(ID3D12Device* device) noexcept;

    void ResetBindings(void) noexcept;

    void BindSampledImage(uint32_t slot, uint32_t srvIndex) noexcept;

    void BindSampler(uint32_t slot, uint32_t samplerSlot) noexcept;

    void BindStorageBuffer(uint32_t slot, uint32_t uavIndex, ComPtr<ID3D12Resource>* pResource = nullptr, D3D12_RESOURCE_STATES* pState = nullptr) noexcept;

    void BindReadOnlyBuffer(uint32_t slot, uint32_t srvIndex, ComPtr<ID3D12Resource>* pResource = nullptr, D3D12_RESOURCE_STATES* pState = nullptr) noexcept;

    void UnbindBuffer(const D3D12_RESOURCE_STATES* pState) noexcept;

    void TransitionBoundBuffers(ID3D12GraphicsCommandList* list) noexcept;

    bool ApplyBindings(const Shader* shader) noexcept;

    void OnDescriptorHeapChanged(void) noexcept;

    inline void InvalidateTables(void) noexcept {
        for (int i = 0; i < CommandList::kTableCount; ++i)
            m_bindingVersions[i] = ++m_bindingVersionCounter;
    }

    void Destroy(void) noexcept;

    inline ID3D12CommandQueue* GetQueue(void) noexcept {
        return m_cmdQueue.Queue();
    }

    inline CommandQueue& CmdQueue(void) noexcept {
        return m_cmdQueue;
    }

    inline int FrameIndex(void) const noexcept {
        return m_frameIndex;
    }

    inline int FrameCount(void) const noexcept {
        return m_frameCount;
    }

    inline uint64_t FrameNumber(void) const noexcept {
        return m_frameNumber;
    }

    inline void SetFrameIndex(int frameIndex) noexcept {
        if ((frameIndex >= 0) and (frameIndex < m_frameCount))
            m_frameIndex = frameIndex;
    }

    inline void NextFrame(void) noexcept {
        m_frameIndex = (m_frameIndex + 1) % m_frameCount;
    }

    // Advances to (frameIndex >= 0) or next slot (frameIndex < 0), waits for that slot's GPU work
    // to complete, then drains its tracked resources / RTVs. Call once per frame before recording.
    bool BeginFrame(int frameIndex = -1) noexcept;

    // Signals the fence for the current slot. Call once per frame after ExecuteAll().
    void EndFrame(void) noexcept;

    // Init / no-frame path: WaitIdle + drain the current slot, except what lists still recording may reference.
    void Flush(void) noexcept;

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

    // Submits what has been CLOSED so far, in close order, and waits for it. The lists still recording
    // are left alone and go out with the frame as usual. For a CPU readback in mid frame: the draws it
    // wants to read sit in closed lists that have not been submitted yet.
    void ExecutePending(void) noexcept;

    void DrainFrameResources(void) noexcept;

    void NoteRecording(CommandList* cl) noexcept;

    void NoteStopped(CommandList* cl) noexcept;

    CommandList* OldestRecordingList(void) const noexcept;

    void ReleaseFrameResources(void) noexcept;

    bool UsesOrderedCopyList(void) noexcept;

    CommandList* OpenOrderedCopyList(void) noexcept;

    uint64_t ProfilerQueryCount(void) const noexcept;

    void CloseProfilerQueries(bool keepRecording) noexcept;

    // Returns a CommandList. If isTemporary is true, tries to reuse one from m_recycledLists
    // before allocating a new one. Temporary CLs are recycled after ExecuteAll().
    // Caller owns the memory for non-temporary lists.
    // Set to true to print every logged GPU call (DrawInstanced etc.) with file/line to stderr.
    // Defaults to false to avoid flooding the output in normal operation.
#if DBG_DIRECTX
    static bool m_logCalls;
#endif
#if DBG_DIRECTX
    inline void DrawInstanced(UINT vtxCount, UINT instCount, UINT startVtx, UINT startInst, std::source_location loc = std::source_location::current()) noexcept {
        if (m_logCalls)
            fprintf(stderr, "[DI]  %u x%u  %s:%u\n", vtxCount, instCount, loc.file_name(), loc.line());
#else
    inline void DrawInstanced(UINT vtxCount, UINT instCount, UINT startVtx, UINT startInst) noexcept {
#endif
        if (CurrentGfxList())
            CurrentGfxList()->DrawInstanced(vtxCount, instCount, startVtx, startInst);
    }

#if DBG_DIRECTX
    inline void DrawIndexedInstanced(UINT idxCount, UINT instCount, UINT startIdx, INT baseVtx, UINT startInst, std::source_location loc = std::source_location::current()) noexcept {
        if (m_logCalls)
            fprintf(stderr, "[DII] %u x%u  %s:%u\n", idxCount, instCount, loc.file_name(), loc.line());
#else
    inline void DrawIndexedInstanced(UINT idxCount, UINT instCount, UINT startIdx, INT baseVtx, UINT startInst) noexcept {
#endif
        if (CurrentGfxList())
            CurrentGfxList()->DrawIndexedInstanced(idxCount, instCount, startIdx, baseVtx, startInst);
    }

#if DBG_DIRECTX
    inline void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION * dst, UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION * src, const D3D12_BOX * srcBox, std::source_location loc = std::source_location::current()) noexcept {
        if (m_logCalls)
            fprintf(stderr, "[CTR] %s:%u\n", loc.file_name(), loc.line());
#else
    inline void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION * dst, UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION * src, const D3D12_BOX * srcBox) noexcept {
#endif
        if (CurrentGfxList())
            CurrentGfxList()->CopyTextureRegion(dst, dstX, dstY, dstZ, src, srcBox);
    }

#if DBG_DIRECTX
    inline void ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers, std::source_location loc = std::source_location::current()) noexcept {
        if (m_logCalls)
            fprintf(stderr, "[RB]  n=%u  %s:%u\n", numBarriers, loc.file_name(), loc.line());
#else
    inline void ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER * barriers) noexcept {
#endif
        if (CurrentGfxList())
            CurrentGfxList()->ResourceBarrier(numBarriers, barriers);
    }

    CommandList* CreateCmdList(const String& name = "", bool isTemporary = true) noexcept;
};

#define commandListHandler CommandListHandler::Instance()

// =================================================================================================
