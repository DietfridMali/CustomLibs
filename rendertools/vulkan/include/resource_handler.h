#pragma once

#include "vkframework.h"
#include "basesingleton.hpp"
#include "array.hpp"

#include <functional>

// =================================================================================================
// GfxResourceHandler — frame-lifetime cleanup tracker.
//
// In DX12 this also doubled as a one-shot upload-buffer allocator (GetUploadResource), which
// in Vulkan is replaced by direct GfxBuffer use. What remains is the deferred-cleanup role:
// when a resource (image / image view / buffer) becomes obsolete in mid-frame, we must not
// destroy it immediately because the GPU may still reference it from in-flight command
// buffers. TrackCleanup defers a lambda by one frame slot — Cleanup(frameIndex) at the start
// of a new frame slot fires the lambdas that were registered the LAST time this slot was
// active (i.e. after that frame's fence has signalled, so the GPU is provably done).
//
// Used by:
//   • Texture::Destroy and the upload path's image replacement — for EVERY texture, not just the
//     disposable ones: a caller may drop a long-lived texture in mid-frame just as well.
//   • RenderTarget BufferInfo::Release, LinearTexture::Update, GfxBuffer::Destroy.
//   • Anywhere a Vulkan handle outlives its owner by one frame.

class GfxResourceHandler
    : public BaseSingleton<GfxResourceHandler>
{
public:
    static constexpr uint32_t FRAME_COUNT = 2;
    // Tracks whether the singleton is still constructed. PrerenderedText / RenderTarget
    // destructors can run after the handler itself has been torn down by the static-
    // destruction chain — in that case TrackCleanup must fall back to inline execution
    // instead of touching m_cleanupCallbacks (dead vector → crash).
    static inline bool               s_isAlive { false };

    AutoArray<std::function<void()>> m_cleanupCallbacks[FRAME_COUNT];
    AutoArray<uint64_t>              m_cleanupSerials[FRAME_COUNT];
    uint32_t                         m_frameIndex { 0 };
    uint64_t                         m_serial { 0 };
    uint64_t                         m_lastAllocSerial { 0 };

    inline uint64_t NextSerial(void) noexcept {
        return ++m_serial;
    }

    inline void NoteFrameAllocation(void) noexcept {
        m_lastAllocSerial = ++m_serial;
    }

    inline uint64_t LastAllocSerial(void) const noexcept {
        return m_lastAllocSerial;
    }

    GfxResourceHandler() noexcept { s_isAlive = true; }
    ~GfxResourceHandler() noexcept {
        // Flush both slots so deferred lambdas don't leak when the handler dies.
        Cleanup(0);
        Cleanup(1);
        s_isAlive = false;
    }

    // Register a destructor lambda for the currently active frame slot. Fires next time this
    // slot becomes active again (one full frame later). After the handler has been destroyed
    // (s_isAlive=false), the callback runs inline — the GPU is idle by then so direct release
    // is safe.
    inline void TrackCleanup(std::function<void()> cleanup) noexcept {
        if (not cleanup)
            return;
        if (not s_isAlive) {
            cleanup();
            return;
        }
        m_cleanupCallbacks[m_frameIndex].Append(std::move(cleanup));
        m_cleanupSerials[m_frameIndex].Append(NextSerial());
    }

    // Execute all pending callbacks for the given slot, then clear the list. Called from
    // CommandQueue::BeginFrame after the slot's in-flight fence has signalled.
    inline void Cleanup(uint32_t frameIndex) noexcept {
        CleanupBefore(frameIndex, UINT64_MAX);
    }

    inline void CleanupBefore(uint32_t frameIndex, uint64_t serialLimit) noexcept {
        if (frameIndex >= FRAME_COUNT)
            return;
        m_frameIndex = frameIndex;
        // Move the pending list out before firing callbacks: a callback may re-enter
        // TrackCleanup (e.g. GfxBuffer::Destroy), which would Append into this very list and
        // invalidate the range-for iterators. Re-entrant registrations land in the now-empty
        // member slot and fire on the next sweep.
        AutoArray<std::function<void()>> cbs = std::move(m_cleanupCallbacks[frameIndex]);
        AutoArray<uint64_t> serials = std::move(m_cleanupSerials[frameIndex]);
        m_cleanupCallbacks[frameIndex].Clear();
        m_cleanupSerials[frameIndex].Clear();
        for (int32_t i = 0; i < cbs.Length(); ++i) {
            if (serials[i] < serialLimit) {
                if (cbs[i])
                    cbs[i]();
            }
            else {
                m_cleanupCallbacks[frameIndex].Append(std::move(cbs[i]));
                m_cleanupSerials[frameIndex].Append(serials[i]);
            }
        }
    }

    inline uint32_t FrameIndex(void) const noexcept { return m_frameIndex; }
};

#define gfxResourceHandler GfxResourceHandler::Instance()

// =================================================================================================
