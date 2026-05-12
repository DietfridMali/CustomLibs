#pragma once

#include "vkframework.h"
#include "string.hpp"
#include "array.hpp"
#include "list.hpp"
#include "basesingleton.hpp"
#include "renderstates.h"
#include "image_layout_tracker.h"

#include <functional>

#ifdef _DEBUG
#include <source_location>
#include <cstdio>
#endif

class Shader;

// =================================================================================================
// Frame protocol (Vulkan):
//   CommandListHandler::ExecuteAll()  - vkQueueSubmit2 over all registered command buffers
//   CommandQueue::EndFrame()          - vkQueuePresentKHR + advance frame slot
//   CommandQueue::BeginFrame()        - vkWaitForFences on inFlight[slot] + vkAcquireNextImageKHR

class CommandQueue
{
public:
    static constexpr uint32_t FRAME_COUNT = 2;

    VkQueue        m_graphicsQueue    { VK_NULL_HANDLE };
    VkQueue        m_presentQueue     { VK_NULL_HANDLE };
    uint32_t       m_graphicsFamily   { 0 };
    uint32_t       m_presentFamily    { 0 };

    VkSemaphore    m_imageAvailable[FRAME_COUNT] { };
    VkSemaphore    m_renderFinished[FRAME_COUNT] { };
    VkFence        m_inFlight       [FRAME_COUNT] { };

    VkDevice       m_device      { VK_NULL_HANDLE };
    VkSwapchainKHR m_swapchain   { VK_NULL_HANDLE };

    uint32_t       m_frameIndex  { 0 };
    uint32_t       m_imageIndex  { 0 };

    bool Create(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
                uint32_t graphicsFamily, uint32_t presentFamily,
                const String& name = "") noexcept;

    bool InitSyncObjects(VkSwapchainKHR swapchain) noexcept;

    void Destroy(void) noexcept;

    bool BeginFrame(void) noexcept;
    void EndFrame(void) noexcept;

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

    VkCommandBuffer CmdBuffer(void) const noexcept;

private:
    bool CreateSyncObjects(void) noexcept;
    void DestroySyncObjects(void) noexcept;
    bool AcquireNextImage(void) noexcept;
    void Present(void) noexcept;
};

// =================================================================================================
// CommandList - Vulkan equivalent of the DX12 ID3D12GraphicsCommandList wrapper.
//
// Each CommandList owns FRAME_COUNT VkCommandPools (one per round-robin frame slot) plus one
// VkCommandBuffer allocated from each pool. Open() switches to the active frame's pool/CB,
// resets it via vkResetCommandPool, and starts recording with vkBeginCommandBuffer. Close()
// ends recording. Flush() submits + waits idle for one-shot temp lists.
//
// The DX12 ID3D12CommandAllocator <-> VkCommandPool mapping is 1:1: an allocator/pool owns
// one or more command lists/buffers and can be reset to recycle them. We allocate exactly
// one CB per pool to match the DX12 layout exactly.

class CommandList
{
public:
    static constexpr uint32_t FRAME_COUNT = 2;

    VkCommandPool                       m_pools[FRAME_COUNT]   { };
    VkCommandBuffer                     m_cmdBuffers[FRAME_COUNT] { };
    bool                                m_isRecording  { false };
    bool                                m_isFlushed    { false };
    bool                                m_isTemporary  { false };
    AutoArray<std::function<void()>>    m_disposableResources;
    uint64_t                            m_id           { 0 };
    uint64_t                            m_executionCounter { 0 };
    uint32_t                            m_refCounter   { 1 };
    String                              m_name         { "" };
    VkPipeline                          m_activePipeline { VK_NULL_HANDLE };

    static List<RenderStates>           m_renderStateStack;

    static void PushRenderStates(void) noexcept;
    static void PopRenderStates(void) noexcept;

    bool Create(const String& name = "", bool isTemporary = false) noexcept;
    void Destroy(void) noexcept;
    void Reset(void) noexcept;

    bool Open(bool saveRenderStates = true) noexcept;
    void Close(bool restoreRenderStates = true) noexcept;
    void Flush(void) noexcept;

    inline void AddResource(std::function<void()> fn) {
        m_disposableResources.Append(std::move(fn));
    }

    void DisposeResources(void) noexcept;

    void SetBarrier(VkImage image, ImageLayoutTracker& tracker, VkImageLayout newLayout,
                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

    void SetBarrier(const VkImageMemoryBarrier2* barriers, int count);

    inline VkCommandBuffer GfxList(bool ignoreState = false) const noexcept {
        if (not (ignoreState or m_isRecording))
            return VK_NULL_HANDLE;
        return m_cmdBuffers[ActiveFrameIndex()];
    }

    inline uint64_t GetId(void) const noexcept { return m_id; }
    inline String GetName(void) const noexcept { return m_name; }
    inline void SetName(String name) noexcept { m_name = name; }
    inline uint64_t GetExecutionCounter(void) const noexcept { return m_executionCounter; }
    inline bool IsRecording(void) const noexcept { return m_isRecording; }
    inline bool IsFlushed(void) const noexcept { return m_isFlushed; }
    inline bool IsTemporary(void) const noexcept { return m_isTemporary; }
    inline void SetTemporary(bool value) noexcept { m_isTemporary = value; }

    void SetActivePipeline(VkPipeline pipeline, Shader* shader) noexcept;

    VkPipeline GetPipeline(Shader* shader) noexcept;

#ifdef _DEBUG
    void CheckDeviceRemoved(const char* context) noexcept;
#endif

private:
    static uint32_t ActiveFrameIndex(void) noexcept;
};

// =================================================================================================
// CommandListHandler - singleton routing the active CommandList stack and driving frame-end
// submission. Identical state-machine to the DX12 path; the DX12 inline draw/copy/barrier
// wrappers are reproduced here using the matching vkCmd entry points.

struct CommandListData {
    CommandList*    cmdList { nullptr };
    VkCommandBuffer cmdBuf  { VK_NULL_HANDLE };
};


class CommandListHandler
    : public BaseSingleton<CommandListHandler>
{
public:
    CommandQueue                        m_cmdQueue;
    AutoArray<CommandList*>             m_pendingLists;
    AutoArray<CommandList*>             m_recycledLists;
    AutoArray<CommandListData>          m_cmdListStack;
    CommandListData                     m_currentListData;
    uint64_t                            m_cmdListId    { 1 };
    uint64_t                            m_cmdListCount { 0 };

#ifdef _DEBUG
    static bool s_logCalls;
#endif

    // -------------------------------------------------------------------------
    // Bind table — CPU-side staging of per-draw shader resource bindings.
    //
    // Texture::Bind / RenderTarget::BindBuffer / GfxArray::Bind populate this table at the
    // logical t/s/u slot. Shader::UpdateVariables (right before each draw) materializes the
    // table into a VkDescriptorSet via descriptorPoolHandler.Allocate + vkUpdateDescriptorSets,
    // then vkCmdBindDescriptorSets binds the set with the b0/b1 dynamic offsets.
    //
    // Reset() at frame start invalidates all slots (cleared to VK_NULL_HANDLE). Slots that
    // remain null at materialize time are simply not written, which mirrors the DX12 path
    // (unbound slots stay at whatever the previous draw left in the descriptor heap).

    static constexpr uint32_t kSrvSlots     = 16;  // matches Shader::kSrvSlots
    static constexpr uint32_t kSamplerSlots = 16;  // matches Shader::kSamplerSlots
    static constexpr uint32_t kUavSlots     = 4;   // matches Shader::kUavSlots

    VkImageView m_boundSrvViews    [kSrvSlots]     { };
    VkSampler   m_boundSamplers    [kSamplerSlots] { };
    VkImageView m_boundStorageViews[kUavSlots]     { };

    void ResetBindings(void) noexcept;
    void BindSampledImage(uint32_t slot, VkImageView view) noexcept;
    void BindSampler(uint32_t slot, VkSampler sampler) noexcept;
    void BindStorageImage(uint32_t slot, VkImageView view) noexcept;

    bool Create(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
                uint32_t graphicsFamily, uint32_t presentFamily,
                const String& name = "MainQueue") noexcept;

    void Destroy(void) noexcept;

    inline VkQueue GetQueue(void) noexcept {
        return m_cmdQueue.GraphicsQueue();
    }

    inline CommandQueue& CmdQueue(void) noexcept {
        return m_cmdQueue;
    }

    inline VkQueue GraphicsQueue(void) const noexcept {
        return m_cmdQueue.GraphicsQueue();
    }

    inline VkCommandBuffer CurrentGfxList(void) const noexcept {
        return m_currentListData.cmdBuf;
    }

    inline CommandList* CurrentCmdList(void) const noexcept {
        return m_currentListData.cmdList;
    }

    void PushCmdList(CommandList* cl) noexcept;
    void PopCmdList(void) noexcept;

    void Register(CommandList* cl) noexcept;

    void ExecuteAll(bool intermediate = false) noexcept;

    CommandList* CreateCmdList(const String& name = "", bool isTemporary = true) noexcept;

#ifdef _DEBUG
    inline void DrawInstanced(uint32_t vtxCount, uint32_t instCount, uint32_t startVtx, uint32_t startInst,
                              std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[DI]  %u x%u  %s:%u\n", vtxCount, instCount, loc.file_name(), (unsigned)loc.line());
#else
    inline void DrawInstanced(uint32_t vtxCount, uint32_t instCount, uint32_t startVtx, uint32_t startInst) noexcept {
#endif
        VkCommandBuffer cb = CurrentGfxList();
        if (cb != VK_NULL_HANDLE)
            vkCmdDraw(cb, vtxCount, instCount, startVtx, startInst);
    }

#ifdef _DEBUG
    inline void DrawIndexedInstanced(uint32_t idxCount, uint32_t instCount, uint32_t startIdx, int32_t baseVtx, uint32_t startInst,
                                     std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[DII] %u x%u  %s:%u\n", idxCount, instCount, loc.file_name(), (unsigned)loc.line());
#else
    inline void DrawIndexedInstanced(uint32_t idxCount, uint32_t instCount, uint32_t startIdx, int32_t baseVtx, uint32_t startInst) noexcept {
#endif
        VkCommandBuffer cb = CurrentGfxList();
        if (cb != VK_NULL_HANDLE)
            vkCmdDrawIndexed(cb, idxCount, instCount, startIdx, baseVtx, startInst);
    }

#ifdef _DEBUG
    inline void CopyImage(VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout,
                          uint32_t regionCount, const VkImageCopy* regions,
                          std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[CI]  %s:%u\n", loc.file_name(), (unsigned)loc.line());
#else
    inline void CopyImage(VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout,
                          uint32_t regionCount, const VkImageCopy* regions) noexcept {
#endif
        VkCommandBuffer cb = CurrentGfxList();
        if (cb != VK_NULL_HANDLE)
            vkCmdCopyImage(cb, src, srcLayout, dst, dstLayout, regionCount, regions);
    }

#ifdef _DEBUG
    inline void PipelineBarrier2(const VkDependencyInfo* dep,
                                 std::source_location loc = std::source_location::current()) noexcept {
        if (s_logCalls)
            fprintf(stderr, "[PB2] %s:%u\n", loc.file_name(), (unsigned)loc.line());
#else
    inline void PipelineBarrier2(const VkDependencyInfo* dep) noexcept {
#endif
        VkCommandBuffer cb = CurrentGfxList();
        if (cb != VK_NULL_HANDLE)
            vkCmdPipelineBarrier2(cb, dep);
    }
};

#define commandListHandler CommandListHandler::Instance()

// =================================================================================================