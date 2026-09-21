#pragma once

#include "vkframework.h"
#include "vkcontext.h"
#include "vkupload.h"
#include "commandlist.h"
#include "resource_handler.h"
#include "shader.h"
#include "array.hpp"
#include "gfxtypes.h"

#include <cstdio>
#include <cstring>

// =================================================================================================
// Vulkan GfxArray — flat R32_UINT storage buffer (analogue of the DX12 UAV-style Texture2D used by
// decalhandler::m_decalDepths). 2D indexing is folded into the shader as `y * width + x`.
//
// Storage-buffer was chosen over storage-image to:
//   • avoid the image-layout transitions GENERAL <-> TRANSFER_DST around every Clear (two
//     vkCmdPipelineBarrier2 per call),
//   • use vkCmdFillBuffer for the clear (much faster than vkCmdClearColorImage on most GPUs —
//     no layout decoding, no 2D tiling),
//   • keep the descriptor type consistent for atomic ops; SPIR-V InterlockedMin works on
//     RWStructuredBuffer<uint> just as on RWTexture2D<uint>.
//
// The clear path still needs the active render-pass scope paused (vkCmdFillBuffer is forbidden
// inside vkCmdBeginRendering exactly like vkCmdClearColorImage); the caller is responsible for
// that — see DecalHandler::Render's EndRendering/BeginRendering bracketing.
//
// Bind() writes (m_buffer, range) into the CommandListHandler bind table at the requested u-slot.
// Shader::UpdateVariables materializes the table into a VkDescriptorSet (vkUpdateDescriptorSets
// + vkCmdBindDescriptorSets) right before each draw.

class BaseGfxArray {
public:
    static bool IsAvailable(void) { return true; }
};

template <typename DATA_T, typename STORAGE_T = GfxTypes::UavTexture>
class GfxArray : public BaseGfxArray
{
public:
    AutoArray<DATA_T>      m_data;

    VkBuffer               m_buffer         { VK_NULL_HANDLE };
    VmaAllocation          m_allocation     { VK_NULL_HANDLE };
    VkDeviceSize           m_bufferSize     { 0 };

    // Lazy-allocated host-visible staging buffers, kept across calls so repeat Upload/Download
    // doesn't churn allocations. The upload staging rotates FRAME_COUNT slots (indexed by the
    // active frame): a CL-aware Upload records the staging->device copy into the frame command
    // list, which executes only at submit. With FRAME_COUNT frames in flight, the next frame's
    // memcpy must not clobber a staging buffer whose copy has not run yet — so each frame slot
    // owns its own staging buffer. Same idiom as GfxDataBuffer's per-frame upload-buffer slots;
    // the inFlight fence guarantees a slot's GPU copy has completed before that slot is reused.
    VkBuffer               m_uploadBuffer[CommandQueue::FRAME_COUNT] { };
    VmaAllocation          m_uploadAlloc [CommandQueue::FRAME_COUNT] { };
    VkBuffer               m_readbackBuffer { VK_NULL_HANDLE };
    VmaAllocation          m_readbackAlloc  { VK_NULL_HANDLE };

    uint32_t               m_width          { 0 };
    uint32_t               m_height         { 0 };
    uint32_t               m_bindingPoint   { 0 };
    uint64_t               m_appendFrame    { UINT64_MAX };
    int                    m_appendBase     { 0 };
    // What of a slot's staging buffer holds data whose copy has been recorded in the current frame but
    // has not run yet - see AcquireStaging ().
    uint64_t               m_stagedFrame[CommandQueue::FRAME_COUNT] { };
    VkDeviceSize           m_stagedMin  [CommandQueue::FRAME_COUNT] { };
    VkDeviceSize           m_stagedMax  [CommandQueue::FRAME_COUNT] { };

    GfxArray() = default;

    ~GfxArray() { Destroy(); }

    inline DATA_T* Data(void) { return m_data.Data(); }
    inline int     DataSize(void) { return m_data.DataSize(); }

    int AppendBase(void) noexcept {
        uint64_t frame = commandListHandler.CmdQueue().FrameNumber();
        if (frame != m_appendFrame) {
            m_appendFrame = frame;
            m_appendBase = 0;
        }
        return m_appendBase;
    }

    void SetAppendBase(int base) noexcept {
        AppendBase();
        if (base > m_appendBase)
            m_appendBase = base;
    }

    void ReleaseDeferred(void) {
        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return;
        if (m_buffer != VK_NULL_HANDLE) {
            // While it still is a valid handle: a buffer that is about to be freed must not stay in a
            // binding slot. The cleanup is deferred, the binding is not - whatever materializes a
            // descriptor set next would write a dead buffer into it. Same as the DX12 Destroy ().
            commandListHandler.UnbindBuffer(m_buffer);
            gfxResourceHandler.TrackCleanup([allocator, buffer = m_buffer, allocation = m_allocation]() {
                vmaDestroyBuffer(allocator, buffer, allocation);
            });
            m_buffer = VK_NULL_HANDLE;
            m_allocation = VK_NULL_HANDLE;
        }
        for (uint32_t i = 0; i < CommandQueue::FRAME_COUNT; ++i) {
            if (m_uploadBuffer[i] != VK_NULL_HANDLE) {
                gfxResourceHandler.TrackCleanup([allocator, buffer = m_uploadBuffer[i], allocation = m_uploadAlloc[i]]() {
                    vmaDestroyBuffer(allocator, buffer, allocation);
                });
                m_uploadBuffer[i] = VK_NULL_HANDLE;
                m_uploadAlloc[i] = VK_NULL_HANDLE;
            }
        }
        if (m_readbackBuffer != VK_NULL_HANDLE) {
            gfxResourceHandler.TrackCleanup([allocator, buffer = m_readbackBuffer, allocation = m_readbackAlloc]() {
                vmaDestroyBuffer(allocator, buffer, allocation);
            });
            m_readbackBuffer = VK_NULL_HANDLE;
            m_readbackAlloc = VK_NULL_HANDLE;
        }
    }

    bool Create(int width, int height = 1) {
        Destroy();
        m_width  = uint32_t(width);
        m_height = uint32_t(height);
        const int size = width * height;
        m_data.Resize(size);

        VmaAllocator allocator = vkContext.Allocator();
        VkDevice     device    = vkContext.Device();
        if ((allocator == VK_NULL_HANDLE) or (device == VK_NULL_HANDLE) or (size == 0))
            return false;

        m_bufferSize = VkDeviceSize(size) * VkDeviceSize(sizeof(DATA_T));

        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = m_bufferSize;
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                       | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        if (vmaCreateBuffer(allocator, &bi, &ai, &m_buffer, &m_allocation, nullptr) != VK_SUCCESS)
            return false;

        return true;
    }

    // Deferred, like GfxBuffer::Destroy (): the frame that is being recorded and the one still in flight
    // may both reference the buffers through their descriptor sets and recorded copies.
    void Destroy(void) {
        ReleaseDeferred();
        m_data.Reset();
        m_width = m_height = 0;
        m_bufferSize = 0;
        m_appendFrame = UINT64_MAX;
        m_appendBase = 0;
    }

    bool Bind(uint32_t bindingPoint) {
        if ((m_buffer == VK_NULL_HANDLE) or (bindingPoint >= CommandListHandler::kUavSlots))
            return false;
        m_bindingPoint = bindingPoint;
        commandListHandler.BindStorageBuffer(bindingPoint, m_buffer, m_bufferSize);
        return true;
    }

    void Release(uint32_t bindingPoint) {
        if (bindingPoint < CommandListHandler::kUavSlots)
            commandListHandler.BindStorageBuffer(bindingPoint, VK_NULL_HANDLE, 0);
    }

    bool BindReadOnly(uint32_t bindingPoint) {
        if ((m_buffer == VK_NULL_HANDLE) or (bindingPoint >= CommandListHandler::kSsboSlots))
            return false;
        commandListHandler.BindReadOnlyBuffer(bindingPoint, m_buffer, m_bufferSize);
        return true;
    }

    void ReleaseReadOnly(uint32_t bindingPoint) {
        if (bindingPoint < CommandListHandler::kSsboSlots)
            commandListHandler.BindReadOnlyBuffer(bindingPoint, VK_NULL_HANDLE, 0);
    }

    // Writes vkCmdFillBuffer onto the currently active CommandList's CB. Contract: caller ensures
    // no vkCmdBeginRendering scope is open at call time — render-pass scopes forbid both buffer
    // fills and image clears. DecalHandler::Render handles that by calling EndRendering before
    // and BeginRendering after the clear.
    void Clear(DATA_T value) {
        if (m_buffer == VK_NULL_HANDLE)
            return;

        VkCommandBuffer cb = commandListHandler.CurrentGfxList();
        // vkCmdFillBuffer fills with a uint32 pattern repeated across the range. For DATA_T larger
        // than 4 bytes the caller would need a different fill primitive; for the decal-depths
        // case (R32_UINT, value = 0xFFFFFFFF) the pattern matches the element layout exactly.
        static_assert(sizeof(DATA_T) == sizeof(uint32_t),
                      "GfxArray::Clear assumes 32-bit elements; widen the fill path for other sizes.");

        // vkCmdFillBuffer is a transfer-stage write with no implicit ordering against the
        // fragment-shader storage accesses around it (the decal mask's InterlockedMin write in
        // pass 0 / read in pass 1). Two buffer barriers make the clear self-synchronizing — legal
        // here because Clear is always called outside a dynamic-rendering scope (DecalHandler::Render
        // brackets it with EndRendering/BeginRendering). GfxStates::SetMemoryBarrier only covers the
        // fragment->fragment pass-0->pass-1 dependency and never names the transfer stage:
        //   • before: a prior shader pass (the previous decal's pass-1 read) must finish before this
        //     fill overwrites the buffer (WAR);
        //   • after: the fill must finish before the next shader pass reads/writes it (RAW) — the
        //     analogue of the DX12 UAV barrier after ClearUnorderedAccessViewUint.
        auto BufferBarrier = [&](VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
            VkBufferMemoryBarrier2 b{};
            b.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b.srcStageMask        = srcStage;
            b.srcAccessMask       = srcAccess;
            b.dstStageMask        = dstStage;
            b.dstAccessMask       = dstAccess;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.buffer              = m_buffer;
            b.offset              = 0;
            b.size                = m_bufferSize;
            VkDependencyInfo dep{};
            dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = 1;
            dep.pBufferMemoryBarriers    = &b;
            vkCmdPipelineBarrier2(cb, &dep);
        };

        BufferBarrier(VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdFillBuffer(cb, m_buffer, 0, m_bufferSize, uint32_t(value));
        BufferBarrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    }

    bool Upload(void) {
        if (m_buffer == VK_NULL_HANDLE or m_data.IsEmpty())
            return false;

        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        uint32_t fi = commandListHandler.CmdQueue().FrameIndex();
        if (not AcquireStaging(fi, 0, m_bufferSize, true))
            return false;

        void* mapped = nullptr;
        if (vmaMapMemory(allocator, m_uploadAlloc[fi], &mapped) != VK_SUCCESS)
            return false;
        std::memcpy(mapped, m_data.Data(), size_t(m_bufferSize));
        vmaUnmapMemory(allocator, m_uploadAlloc[fi]);

        return CopyFromStaging(m_uploadBuffer[fi], 0, 0, m_bufferSize, true);
    }

    // Partial upload of [first, first+count) elements only — lets the particle handler push freshly
    // spawned systems without re-uploading (and thereby resetting) the live systems' GPU state.
    // ordered: the copy has to take effect where it is issued in the frame's recording order (a
    // buffer that is rewritten between passes). false for a caller whose ranges never overlap within
    // a frame (LineRenderer): its copies may run ahead of the frame, which spares the rendering scope
    // being closed and reopened around every one of them.
    bool UploadRange(int first, int count, bool ordered = true) {
        if ((m_buffer == VK_NULL_HANDLE) or m_data.IsEmpty() or (count <= 0) or (first < 0))
            return false;

        VkDeviceSize elemSize = VkDeviceSize(sizeof(DATA_T));
        VkDeviceSize dstOffset = VkDeviceSize(first) * elemSize;
        VkDeviceSize bytes = VkDeviceSize(count) * elemSize;
        if (dstOffset + bytes > m_bufferSize)
            return false;

        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        // The per-frame staging buffer mirrors the FULL device-buffer width, and the range is written
        // at its real dstOffset (not 0). Several UploadRange calls in one frame (e.g. an explosion's
        // body + filament slots) then land at distinct staging offsets, so the second memcpy cannot
        // clobber the first slot's not-yet-executed copy. Costs one full-width host-visible buffer per
        // frame slot; the per-frame rotation still guards against frames-in-flight reuse.
        uint32_t fi = commandListHandler.CmdQueue().FrameIndex();
        if (not AcquireStaging(fi, dstOffset, bytes, ordered))
            return false;

        void* mapped = nullptr;
        if (vmaMapMemory(allocator, m_uploadAlloc[fi], &mapped) != VK_SUCCESS)
            return false;
        std::memcpy(static_cast<uint8_t*>(mapped) + dstOffset, m_data.Data() + first, size_t(bytes));
        vmaUnmapMemory(allocator, m_uploadAlloc[fi]);

        return CopyFromStaging(m_uploadBuffer[fi], dstOffset, dstOffset, bytes, ordered);
    }

    bool Download(void) {
        if (m_buffer == VK_NULL_HANDLE or m_data.IsEmpty())
            return false;

        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        if (not EnsureStagingBuffer(m_readbackBuffer, m_readbackAlloc, m_bufferSize,
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT))
            return false;

        OneShotCommandBuffer once;
        if (not BeginSingleTimeCommands(once))
            return false;

        VkBufferCopy region{};
        region.size = m_bufferSize;
        vkCmdCopyBuffer(once.cb, m_buffer, m_readbackBuffer, 1, &region);

        if (not EndSingleTimeCommands(once))
            return false;

        void* mapped = nullptr;
        if (vmaMapMemory(allocator, m_readbackAlloc, &mapped) != VK_SUCCESS)
            return false;
        std::memcpy(m_data.Data(), mapped, size_t(m_bufferSize));
        vmaUnmapMemory(allocator, m_readbackAlloc);
        return true;
    }

private:
    // Issues the staging -> GPU copy from the caller-selected per-frame staging buffer. Mid-frame
    // (a per-frame command list is open) it records into that list with a transfer -> shader barrier
    // (like the DX path), so no blocking one-shot submit + vkQueueWaitIdle corrupts the in-flight
    // frame. While a rendering scope is open on that list a copy is illegal there. An ordered copy has
    // the scope closed around it and reopened with its contents kept, so it stays where it was issued;
    // an unordered one goes into the handler's detached upload list instead
    // (CommandListHandler::UploadCmdBuffer ()), which is submitted ahead of the list that is recording.
    // Only at setup time (no open CL) does it fall back to the one-shot path.
    bool CopyFromStaging(VkBuffer staging, VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize bytes, bool ordered) {
        VkBufferCopy region{};
        region.srcOffset = srcOffset;
        region.dstOffset = dstOffset;
        region.size = bytes;

        CommandList* copyList = nullptr;
        if (ordered and commandListHandler.UsesOrderedCopyList()) {
            if (not (copyList = commandListHandler.OpenOrderedCopyList()))
                return false;
        }
        VkCommandBuffer frameCB = copyList ? copyList->GfxList() : commandListHandler.CurrentGfxList();
        CommandListHandler::RenderingScope scope;
        if (not copyList and (frameCB != VK_NULL_HANDLE) and commandListHandler.IsInRendering()) {
            if (ordered)
                scope = commandListHandler.SuspendRendering();
            else {
                frameCB = commandListHandler.UploadCmdBuffer();
                if (frameCB == VK_NULL_HANDLE)
                    return false;
            }
        }
        if (frameCB != VK_NULL_HANDLE) {
            vkCmdCopyBuffer(frameCB, staging, m_buffer, 1, &region);
            VkBufferMemoryBarrier2 b{};
            b.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b.srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            b.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b.dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                                  | VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT
                                  | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask       = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.buffer              = m_buffer;
            b.offset              = dstOffset;
            b.size                = bytes;
            VkDependencyInfo dep{};
            dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = 1;
            dep.pBufferMemoryBarriers    = &b;
            vkCmdPipelineBarrier2(frameCB, &dep);
            commandListHandler.ResumeRendering(scope);
            if (copyList)
                copyList->Close(false);
            return true;
        }

        OneShotCommandBuffer once;
        if (not BeginSingleTimeCommands(once))
            return false;
        vkCmdCopyBuffer(once.cb, staging, m_buffer, 1, &region);
        EndSingleTimeCommands(once);
        return true;
    }

    // The frame slot's staging buffer for an upload of [offset, offset + bytes). A recorded copy runs
    // when the frame is submitted, so a second upload in the same frame that overlaps a range already
    // staged would replace the data the first copy is still going to read - a buffer rewritten between
    // two passes of one frame would arrive with the last pass's contents in both. Such an upload gets a
    // staging buffer of its own; the old one is released once the frame is through. Disjoint ranges
    // (LineRenderer's appended batches) share the buffer. Without an open command list the copy runs at
    // once (one-shot) and nothing has to be kept apart.
    bool AcquireStaging(uint32_t fi, VkDeviceSize offset, VkDeviceSize bytes, bool ordered) noexcept {
        const uint64_t frame = commandListHandler.CmdQueue().FrameNumber();
        const bool isDeferred = (commandListHandler.CurrentGfxList() != VK_NULL_HANDLE) or (ordered and commandListHandler.UsesOrderedCopyList());
        bool sameFrame = isDeferred and (m_uploadBuffer[fi] != VK_NULL_HANDLE) and (m_stagedFrame[fi] == frame) and (m_stagedMax[fi] > m_stagedMin[fi]);
        if (sameFrame and (offset < m_stagedMax[fi]) and (offset + bytes > m_stagedMin[fi])) {
            VmaAllocator allocator = vkContext.Allocator();
            gfxResourceHandler.TrackCleanup([allocator, buffer = m_uploadBuffer[fi], allocation = m_uploadAlloc[fi]]() {
                vmaDestroyBuffer(allocator, buffer, allocation);
            });
            m_uploadBuffer[fi] = VK_NULL_HANDLE;
            m_uploadAlloc[fi] = VK_NULL_HANDLE;
            sameFrame = false;
        }
        if (not EnsureStagingBuffer(m_uploadBuffer[fi], m_uploadAlloc[fi], m_bufferSize,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))
            return false;
        if (not isDeferred) {
            m_stagedMin[fi] = 0;
            m_stagedMax[fi] = 0;
        }
        else if (sameFrame) {
            if (offset < m_stagedMin[fi])
                m_stagedMin[fi] = offset;
            if (offset + bytes > m_stagedMax[fi])
                m_stagedMax[fi] = offset + bytes;
        }
        else {
            m_stagedMin[fi] = offset;
            m_stagedMax[fi] = offset + bytes;
        }
        m_stagedFrame[fi] = frame;
        return true;
    }

    bool EnsureStagingBuffer(VkBuffer& outBuffer, VmaAllocation& outAlloc, VkDeviceSize bytes,
                             VkBufferUsageFlags usage, VmaAllocationCreateFlags hostFlags) noexcept
    {
        VmaAllocator allocator = vkContext.Allocator();
        if (allocator == VK_NULL_HANDLE)
            return false;

        if (outBuffer != VK_NULL_HANDLE) {
            VmaAllocationInfo info{};
            vmaGetAllocationInfo(allocator, outAlloc, &info);
            if (info.size >= bytes)
                return true;
            vmaDestroyBuffer(allocator, outBuffer, outAlloc);
            outBuffer = VK_NULL_HANDLE;
            outAlloc  = VK_NULL_HANDLE;
        }

        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = bytes;
        bi.usage       = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = hostFlags;

        return vmaCreateBuffer(allocator, &bi, &ai, &outBuffer, &outAlloc, nullptr) == VK_SUCCESS;
    }
};

// =================================================================================================
