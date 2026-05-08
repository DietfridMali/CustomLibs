#pragma once

#include "vkframework.h"
#include "string.hpp"

// =================================================================================================
// Frame protocol (Vulkan):
//   CommandListHandler::ExecuteAll()  — submit all registered lists via vkQueueSubmit2 (Phase C)
//   CommandQueue::EndFrame()          — vkQueuePresentKHR + advance frame index
//   CommandQueue::BeginFrame()        — wait inFlight[slot] + vkAcquireNextImageKHR
//
// CommandQueue is the FrameSync wrapper. It carries:
//   - the graphics + present queue handles (separate; can point to the same VkQueue),
//   - per-slot swapchain sync objects (imageAvailable, renderFinished, inFlight),
//   - the current sync slot (m_frameIndex, round-robin) and the current swapchain image
//     index (m_imageIndex, returned by AcquireNext, arbitrary).
//
// Sync slot vs swapchain image: callers that key per-frame resources (allocators, descriptor
// pools, ring buffers) use FrameIndex() — the round-robin slot. ImageIndex() is internal,
// used by BaseDisplayHandler to address the right backbuffer for blit/present.

class CommandQueue
{
public:
    static constexpr uint32_t FRAME_COUNT = 2;

    VkQueue        m_graphicsQueue    { VK_NULL_HANDLE };
    VkQueue        m_presentQueue     { VK_NULL_HANDLE };
    uint32_t       m_graphicsFamily   { 0 };
    uint32_t       m_presentFamily    { 0 };

    // Per-slot sync objects, allocated by InitSyncObjects after the swapchain exists.
    VkSemaphore    m_imageAvailable[FRAME_COUNT] { };  // signaled by vkAcquireNextImageKHR
    VkSemaphore    m_renderFinished[FRAME_COUNT] { };  // signaled by vkQueueSubmit2
    VkFence        m_inFlight       [FRAME_COUNT] { };  // signaled by vkQueueSubmit2, waited in BeginFrame

    // Cached for use in BeginFrame / EndFrame. Set by Create / InitSyncObjects.
    VkDevice       m_device      { VK_NULL_HANDLE };
    VkSwapchainKHR m_swapchain   { VK_NULL_HANDLE };

    uint32_t       m_frameIndex  { 0 };  // sync slot, round-robin 0..FRAME_COUNT-1
    uint32_t       m_imageIndex  { 0 };  // swapchain image returned by AcquireNext

    // Records device + queue handles + family indices. No GPU work — sync objects are
    // created later by InitSyncObjects, after the swapchain is known.
    bool Create(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
                uint32_t graphicsFamily, uint32_t presentFamily,
                const String& name = "") noexcept;

    // Allocates per-slot semaphores + fences. Caller passes the swapchain that
    // BeginFrame's vkAcquireNextImageKHR will target.
    bool InitSyncObjects(VkSwapchainKHR swapchain) noexcept;

    void Destroy(void) noexcept;

    // Wait inFlight[m_frameIndex] -> reset -> AcquireNextImage -> writes m_imageIndex.
    bool BeginFrame(void) noexcept;

    // Present (waits on renderFinished[m_frameIndex]) -> advance m_frameIndex.
    void EndFrame(void) noexcept;

    // Local: vkQueueWaitIdle on the graphics queue. 1:1 to DX12 CommandQueue::WaitIdle.
    // GfxStates::Finish uses vkDeviceWaitIdle instead, for full-device synchronization.
    void WaitIdle(void) noexcept;

    inline VkSemaphore SubmitWaitSemaphore(void) const noexcept { return m_imageAvailable[m_frameIndex]; }
    inline VkSemaphore SubmitSignalSemaphore(void) const noexcept { return m_renderFinished[m_frameIndex]; }
    inline VkFence SubmitSignalFence(void) const noexcept { return m_inFlight[m_frameIndex]; }

    inline VkQueue GraphicsQueue(void) const noexcept { return m_graphicsQueue; }
    inline VkQueue PresentQueue(void) const noexcept { return m_presentQueue; }
    inline uint32_t GraphicsFamily(void) const noexcept { return m_graphicsFamily; }
    inline uint32_t PresentFamily(void) const noexcept { return m_presentFamily; }
    inline uint32_t FrameIndex(void) const noexcept { return m_frameIndex; }
    inline uint32_t ImageIndex(void) const noexcept { return m_imageIndex; }

    // Returns the currently active VkCommandBuffer (set by CommandList::Open via PushList).
    // Phase C: routes through CommandListHandler. Phase A stub: VK_NULL_HANDLE.
    VkCommandBuffer CmdBuffer(void) const noexcept;

private:
    bool CreateSyncObjects(void) noexcept;
    void DestroySyncObjects(void) noexcept;
    bool AcquireNextImage(void) noexcept;
    void Present(void) noexcept;
};

// Forward-declare CommandList so headers (rendertarget.h, gfxdatalayout.h) that hold
// CommandList* members can compile in Phase B — the full class lives in the #if 0 block below.
class CommandList;

// =================================================================================================
// CommandListHandler — minimal Phase-B skeleton.
//
// Owns the CommandQueue (FrameSync wrapper). The full CommandList / Pending-list / ExecuteAll /
// per-frame command-buffer pool come in Phase C — those parts stay in the #if 0 block below.
// What is exposed here is what GfxRenderer / BaseDisplayHandler need right now: Create the
// queue, hand the queue out, Destroy on shutdown.

#include "basesingleton.hpp"

class CommandListHandler
    : public BaseSingleton<CommandListHandler>
{
public:
    CommandQueue m_cmdQueue;

    inline CommandQueue& CmdQueue(void) noexcept {
        return m_cmdQueue;
    }

    inline VkQueue GraphicsQueue(void) const noexcept {
        return m_cmdQueue.GraphicsQueue();
    }

    // Records device + queue handles + family indices in the CommandQueue. Sync objects are
    // allocated later by InitSyncObjects (after the swapchain exists).
    bool Create(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
                uint32_t graphicsFamily, uint32_t presentFamily,
                const String& name = "MainQueue") noexcept {
        return m_cmdQueue.Create(device, graphicsQueue, presentQueue,
                                 graphicsFamily, presentFamily, name);
    }

    void Destroy(void) noexcept {
        m_cmdQueue.Destroy();
    }

    // Phase C: CurrentCmdBuffer / CurrentCmdList / Register / ExecuteAll / Draw* wrappers etc.
};

#define commandListHandler CommandListHandler::Instance()

// =================================================================================================
// CommandList / CommandListData / full CommandListHandler API — Phase C.
// The 1:1 DX12 carry-over below is preserved as reference and will be ported when
// the CL/RT lifecycle moves to Vulkan VkCommandBuffer + VkCommandPool semantics.

#if 0

#include "array.hpp"
#include "gfxstates.h"
//#include "shader.h"
#include <functional>

#ifdef _DEBUG
#include <source_location>
#include <cstdio>
#endif

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
    // Set to true to print every logged GPU call (DrawInstanced etc.) with file/line to stderr.
    // Defaults to false to avoid flooding the output in normal operation.
#ifdef _DEBUG
    static bool s_logCalls;
#endif
#ifdef _DEBUG
    inline void DrawInstanced(UINT vtxCount, UINT instCount, UINT startVtx, UINT startInst, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[DI]  %u x%u  %s:%u\n", vtxCount, instCount, loc.file_name(), loc.line());
#else
    inline void DrawInstanced(UINT vtxCount, UINT instCount, UINT startVtx, UINT startInst) noexcept {
#endif
        if (CurrentGfxList())
            CurrentGfxList()->DrawInstanced(vtxCount, instCount, startVtx, startInst);
    }

#ifdef _DEBUG
    inline void DrawIndexedInstanced(UINT idxCount, UINT instCount, UINT startIdx, INT baseVtx, UINT startInst, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[DII] %u x%u  %s:%u\n", idxCount, instCount, loc.file_name(), loc.line());
#else
    inline void DrawIndexedInstanced(UINT idxCount, UINT instCount, UINT startIdx, INT baseVtx, UINT startInst) noexcept {
#endif
        if (CurrentGfxList())
            CurrentGfxList()->DrawIndexedInstanced(idxCount, instCount, startIdx, baseVtx, startInst);
    }

#ifdef _DEBUG
    inline void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION * dst, UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION * src, const D3D12_BOX * srcBox, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[CTR] %s:%u\n", loc.file_name(), loc.line());
#else
    inline void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION * dst, UINT dstX, UINT dstY, UINT dstZ, const D3D12_TEXTURE_COPY_LOCATION * src, const D3D12_BOX * srcBox) noexcept {
#endif
        if (CurrentGfxList())
            CurrentGfxList()->CopyTextureRegion(dst, dstX, dstY, dstZ, src, srcBox);
    }

#ifdef _DEBUG
    inline void ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers, std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
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

#endif // Phase C

// =================================================================================================
