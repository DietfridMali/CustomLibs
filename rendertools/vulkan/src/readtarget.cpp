#include "readtarget.h"
#include "rendertarget.h"
#include "commandlist.h"
#include "resource_handler.h"

// =================================================================================================

bool GfxReadTarget::Request(RenderTarget& renderTarget, int bufferIndex, int arraySlice) {
    if (not IsIdle())
        return false;
    return renderTarget.ReadBufferAsync(bufferIndex, *this, arraySlice);
}


bool GfxReadTarget::Allocate(size_t size) {
    if (size == 0)
        return false;
    if ((m_readback.buffer != VK_NULL_HANDLE) and (m_capacity >= size))
        return true;
    if (m_readback.buffer != VK_NULL_HANDLE) {
        VkStagingBuffer old = m_readback;

        gfxResourceHandler.TrackCleanup([old]() mutable { old.Destroy(); });
        m_readback = VkStagingBuffer{};
        m_capacity = 0;
    }
    if (not CreateReadbackBuffer(VkDeviceSize(size), m_readback))
        return false;
    if (m_readback.mapped == nullptr) {
        m_readback.Destroy();
        m_readback = VkStagingBuffer{};
        return false;
    }
    m_capacity = size;
    return true;
}


bool GfxReadTarget::Submit(size_t size, int width, int height, uint64_t frame, int slot) {
    SetRequest(size, width, height, frame, slot);
    return true;
}


bool GfxReadTarget::IsComplete(void) noexcept {
    CommandQueue& queue = commandListHandler.CmdQueue();
    uint64_t frame = queue.FrameNumber();

    if (frame <= m_requestFrame)
        return false;
    if (frame >= m_requestFrame + uint64_t(CommandQueue::FRAME_COUNT))
        return true;
    if ((queue.m_device == VK_NULL_HANDLE) or (queue.m_inFlight[m_requestSlot] == VK_NULL_HANDLE))
        return false;
    return vkGetFenceStatus(queue.m_device, queue.m_inFlight[m_requestSlot]) == VK_SUCCESS;
}


BaseReadTarget::State GfxReadTarget::Poll(void) {
    if (m_state != State::Pending)
        return m_state;
    if (IsComplete())
        m_state = State::Ready;
    return m_state;
}


const uint8_t* GfxReadTarget::Data(void) {
    return (m_state == State::Ready) ? static_cast<const uint8_t*>(m_readback.mapped) : nullptr;
}


void GfxReadTarget::Release(void) {
    m_state = State::Idle;
}


void GfxReadTarget::Destroy(void) {
    Release();
    if (m_readback.buffer != VK_NULL_HANDLE) {
        VkStagingBuffer old = m_readback;

        gfxResourceHandler.TrackCleanup([old]() mutable { old.Destroy(); });
        m_readback = VkStagingBuffer{};
    }
    m_capacity = 0;
    m_size = 0;
}
