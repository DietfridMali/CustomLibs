#pragma once

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "string.hpp"
#include "gfxstates.h"
#include "shader.h"
#include "descriptor_heap.h"

// =================================================================================================
// GfxResourceHandler: per-frame deferred release of D3D resources and descriptor handles.
//
//   Track(resource) — push a ComPtr<ID3D12Resource> into the current slot.
//   Track(handle)   — push an RTV DescriptorHandle into the current slot (returned to RTV-heap free list on Cleanup).
//   Cleanup(idx, waitIdle = false) — drain slot[idx]; waitIdle=true forces a CPU/GPU sync first
//                                    (init/no-frame path); regular frame uses fence wait at BeginFrame.
//
// Lifecycle is owned by CommandListHandler — Init() resizes the per-slot arrays once m_frameCount
// is known, BeginFrame/Flush drive Cleanup.

class GfxResourceHandler
    : public BaseSingleton<GfxResourceHandler>
{
public:
    using ResourceArray = AutoArray<ComPtr<ID3D12Resource>>;
    using RTVArray      = AutoArray<DescriptorHandle>;

private:
    AutoArray<ResourceArray>    m_frameResources;
    AutoArray<RTVArray>         m_frameRTVs;

public:
    // Resize per-slot arrays to match commandListHandler.FrameCount(). Idempotent — safe to call
    // multiple times.
    void Init(int frameCount) noexcept;

    ComPtr<ID3D12Resource> GetUploadResource(const char* name, size_t dataSize);

    void Track(ComPtr<ID3D12Resource> resource) noexcept;

    void Track(const DescriptorHandle& rtvHandle) noexcept;

    // Drain slot[frameIndex]: free queued RTV descriptor slots, then drop ComPtr resource refs.
    // waitIdle=true issues a full CPU/GPU sync first — only needed when there's no fence wait
    // upstream (init phases, explicit Flush()).
    void Cleanup(int frameIndex, bool waitIdle = false) noexcept;
};

#define gfxResourceHandler GfxResourceHandler::Instance()

// =================================================================================================
