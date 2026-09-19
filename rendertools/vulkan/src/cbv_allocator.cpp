#include "cbv_allocator.h"
#include "resource_handler.h"
#include "vkcontext.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// =================================================================================================
// CbvLinearAllocator — Vulkan implementation

bool CbvLinearAllocator::AllocFrame(uint32_t frameIdx, uint32_t capacity) noexcept
{
    auto& f = m_frames[frameIdx];

    f.buffer.Destroy();
    f.offset = 0;
    f.capacity = 0;

    if (not f.buffer.Create(VkDeviceSize(capacity),
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VMA_MEMORY_USAGE_AUTO,
                            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                          | VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        fprintf(stderr, "CbvLinearAllocator: GfxBuffer::Create[%u] failed (cap=%u)\n", frameIdx, capacity);
        return false;
    }
    f.offset = 0;
    f.capacity = capacity;
    return true;
}


bool CbvLinearAllocator::AddChunk(FrameData& f, uint32_t capacity) noexcept
{
    Chunk chunk;

    if (not chunk.buffer.Create(VkDeviceSize(capacity),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VMA_MEMORY_USAGE_AUTO,
                                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                              | VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        fprintf(stderr, "CbvLinearAllocator: GfxBuffer::Create for chained buffer %u of frame %u failed (cap=%u)\n",
                uint32_t(f.overflow.size()), m_frameIndex, capacity);
        return false;
    }
    chunk.capacity = capacity;
    f.overflow.push_back(chunk);
    f.overflowOffset = 0;
    return true;
}


void CbvLinearAllocator::DestroyChunks(FrameData& f) noexcept
{
    for (auto& chunk : f.overflow)
        chunk.buffer.Destroy();
    f.overflow.clear();
    f.overflowOffset = 0;
}


bool CbvLinearAllocator::Create(void) noexcept
{
    if (vkContext.Device() == VK_NULL_HANDLE)
        return false;

    // Query the device's UBO offset alignment requirement (typically 64 / 256).
    const VkDeviceSize devAlign = vkContext.DeviceProps().limits.minUniformBufferOffsetAlignment;
    m_align = (devAlign > 1) ? uint32_t(devAlign) : 256u;

    for (uint32_t i = 0; i < 2; ++i) {
        if (not AllocFrame(i, kInitCap))
            return false;
    }
    return true;
}


void CbvLinearAllocator::Destroy(void) noexcept
{
    for (auto& f : m_frames) {
        DestroyChunks(f);
        f.buffer.Destroy();
        f.offset = 0;
        f.capacity = 0;
        f.peakOffset = 0;
    }
}


void CbvLinearAllocator::Reset(uint32_t frameIndex) noexcept
{
    m_frameIndex = frameIndex;
    auto& f = m_frames[frameIndex];

    DestroyChunks(f);
    if ((f.peakOffset > f.capacity) and (f.capacity < kMaxCap)) {
        uint32_t newCap = f.capacity;
        while ((newCap < f.peakOffset) and (newCap < kMaxCap))
            newCap *= 2;
        newCap = std::min(newCap, kMaxCap);
        AllocFrame(frameIndex, newCap);
    }
    f.peakOffset = 0;
    f.offset = 0;
}


CbAlloc CbvLinearAllocator::Allocate(uint32_t bytes) noexcept
{
    const uint32_t aligned = (bytes + m_align - 1u) & ~(m_align - 1u);
    auto& f = m_frames[m_frameIndex];

    CbAlloc a;

    if (f.overflow.empty() and (f.offset + aligned <= f.capacity)) {
        a.cpu = static_cast<uint8_t*>(f.buffer.Mapped()) + f.offset;
        a.offset = f.offset;
        a.buffer = f.buffer.Buffer();
        f.offset += aligned;
        f.peakOffset += aligned;
        gfxResourceHandler.NoteFrameAllocation();
        return a;
    }

    if (f.overflow.empty() or (f.overflowOffset + aligned > f.overflow.back().capacity)) {
        if (not AddChunk(f, std::max(f.capacity, aligned)))
            return { };
    }

    Chunk& chunk = f.overflow.back();

    a.cpu = static_cast<uint8_t*>(chunk.buffer.Mapped()) + f.overflowOffset;
    a.offset = f.overflowOffset;
    a.buffer = chunk.buffer.Buffer();
    f.overflowOffset += aligned;
    f.peakOffset += aligned;
    gfxResourceHandler.NoteFrameAllocation();
    return a;
}

// =================================================================================================
