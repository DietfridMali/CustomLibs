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
//   • Texture::Destroy when m_isDisposable is set (one frame of safety after detach).
//   • Anywhere a Vulkan handle outlives its owner by one frame.

class GfxResourceHandler
    : public BaseSingleton<GfxResourceHandler>
{
public:
    static constexpr uint32_t FRAME_COUNT = 2;

    AutoArray<std::function<void()>> m_cleanupCallbacks[FRAME_COUNT];
    uint32_t                         m_frameIndex { 0 };

    // Register a destructor lambda for the currently active frame slot. Fires next time this
    // slot becomes active again (one full frame later).
    inline void TrackCleanup(std::function<void()> cleanup) noexcept {
        if (cleanup)
            m_cleanupCallbacks[m_frameIndex].Append(std::move(cleanup));
    }

    // Execute all pending callbacks for the given slot, then clear the list. Called from
    // CommandQueue::BeginFrame after the slot's in-flight fence has signalled.
    inline void Cleanup(uint32_t frameIndex) noexcept {
        if (frameIndex >= FRAME_COUNT)
            return;
        m_frameIndex = frameIndex;
        auto& list = m_cleanupCallbacks[frameIndex];
        for (auto& cb : list) {
            if (cb)
                cb();
        }
        list.Clear();
    }

    inline uint32_t FrameIndex(void) const noexcept { return m_frameIndex; }
};

#define gfxResourceHandler GfxResourceHandler::Instance()

// =================================================================================================
