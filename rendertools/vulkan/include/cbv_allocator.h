#pragma once

#include "vkframework.h"
#include "basesingleton.hpp"
#include "gfx_buffer.h"

// =================================================================================================
// CbvLinearAllocator — per-frame linear allocator for uniform-buffer sub-allocations.
//
// Vulkan equivalent of the DX12 cbv_allocator. Each frame slot owns one persistent-mapped
// host-visible UBO buffer (via GfxBuffer + VMA); Allocate() hands out a {cpu, dynamicOffset}
// pair that the caller copies its UBO data into and feeds into vkCmdBindDescriptorSets via
// pDynamicOffsets[]. Reset() at frame start rewinds the cursor; if the previous frame
// overshot the capacity, the buffer is reallocated up to kMaxCap.
//
// Usage:
//   App init        →  cbvAllocator.Create()              (allocates two frame buffers)
//   BeginFrame      →  cbvAllocator.Reset(frameIndex)
//   UpdateMatrices  →  auto a = cbvAllocator.Allocate(sizeof(FrameConstants))
//                      memcpy(a.cpu, &m_b0Staging, sizeof(FrameConstants))
//                      // a.offset later goes into vkCmdBindDescriptorSets pDynamicOffsets

struct CbAlloc {
    uint8_t*  cpu     { nullptr };
    uint32_t  offset  { 0 };          // dynamic offset into the current frame buffer

    bool IsValid(void) const noexcept { return cpu != nullptr; }
};


class CbvLinearAllocator : public BaseSingleton<CbvLinearAllocator>
{
    static constexpr uint32_t kInitCap = 512u * 1024u;     // 512 KB per frame slot
    static constexpr uint32_t kMaxCap  = 4u * 1024u * 1024u;  // 4 MB hard ceiling

    struct FrameData {
        GfxBuffer  buffer;
        uint32_t   offset      { 0 };
        uint32_t   capacity    { 0 };
        uint32_t   peakOffset  { 0 };
    };

    FrameData  m_frames[2];   // FRAME_COUNT = 2
    uint32_t   m_frameIndex   { 0 };
    uint32_t   m_align        { 256 };  // queried from device limits at Create

    bool AllocFrame(uint32_t frameIdx, uint32_t capacity) noexcept;

public:
    // Allocates the per-frame UBO buffers. Reads minUniformBufferOffsetAlignment from
    // VKContext::DeviceProps to set the alignment.
    bool Create(void) noexcept;
    void Destroy(void) noexcept;

    // Reset at frame start (after fence wait so the GPU is done with this frame's data).
    // Grows the buffer if the previous frame overshot capacity.
    void Reset(uint32_t frameIndex) noexcept;

    // Allocate 'bytes' (rounded up to m_align) from the current frame's buffer.
    // Returns {nullptr, 0} on overflow (logged to stderr; growth deferred to next Reset).
    CbAlloc Allocate(uint32_t bytes) noexcept;

    inline VkBuffer CurrentBuffer(void) const noexcept {
        return m_frames[m_frameIndex].buffer.Buffer();
    }

    inline uint32_t CurrentFrame(void) const noexcept { return m_frameIndex; }
    inline uint32_t Alignment(void) const noexcept { return m_align; }
};

#define cbvAllocator CbvLinearAllocator::Instance()

// =================================================================================================
