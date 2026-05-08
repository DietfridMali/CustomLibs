#include "cbv_allocator.h"
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

    if (f.peakOffset > f.capacity) {
        uint32_t newCap = f.capacity;
        while ((newCap < f.peakOffset) and (newCap < kMaxCap))
            newCap *= 2;
        newCap = std::min(newCap, kMaxCap);
        if (newCap >= f.peakOffset)
            AllocFrame(frameIndex, newCap);
        else
            fprintf(stderr, "CbvLinearAllocator: frame %u peak %u exceeds kMaxCap %u\n",
                    frameIndex, f.peakOffset, kMaxCap);
    }
    f.peakOffset = 0;
    f.offset = 0;
}


CbAlloc CbvLinearAllocator::Allocate(uint32_t bytes) noexcept
{
    const uint32_t aligned = (bytes + m_align - 1u) & ~(m_align - 1u);
    auto& f = m_frames[m_frameIndex];

    if (f.offset + aligned > f.capacity) {
        fprintf(stderr, "CbvLinearAllocator: frame %u overflow (capacity %u, needed %u) — grow deferred to next Reset\n",
                m_frameIndex, f.capacity, f.offset + aligned);
        if (f.offset + aligned > f.peakOffset)
            f.peakOffset = f.offset + aligned;
        return { };
    }

    CbAlloc a;
    a.cpu = static_cast<uint8_t*>(f.buffer.Mapped()) + f.offset;
    a.offset = f.offset;
    f.offset += aligned;
    if (f.offset > f.peakOffset)
        f.peakOffset = f.offset;
    return a;
}

// =================================================================================================
