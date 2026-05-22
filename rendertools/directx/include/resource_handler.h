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
//   Track(handle)   — push a DescriptorHandle into the current slot (released to its owning heap on Cleanup).
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
    using DescriptorArray      = AutoArray<DescriptorHandle>;

private:
    AutoArray<ResourceArray>    m_frameResources;
    AutoArray<DescriptorArray>         m_frameDescriptors;

    // Set once renderer teardown begins. From then on Track()/Cleanup() are inert: the descriptor
    // heaps are destroyed wholesale at program exit, so per-slot deferred frees during teardown are
    // both unnecessary and unsafe (RenderTargets die at static destruction, racing the singletons
    // this mechanism depends on). static — readable even after the singleton itself is gone.
    static inline bool          s_shutdown{ false };

public:
    // Resize per-slot arrays to match commandListHandler.FrameCount(). Idempotent — safe to call
    // multiple times.
    void Init(int frameCount) noexcept;

    // Marks the start of graphics teardown — see s_shutdown. After this, deferred release is a
    // no-op and BufferInfo::Release drops descriptors directly. static so it is safe to query
    // during static-destruction order races.
    static void BeginShutdown(void) noexcept { s_shutdown = true; }

    static bool IsShuttingDown(void) noexcept { return s_shutdown; }

    // Drain every frame slot at once — called from renderer shutdown while the descriptor heaps
    // are still alive, before BeginShutdown().
    void CleanupAll(void) noexcept;

    ComPtr<ID3D12Resource> GetUploadResource(const char* name, size_t dataSize);

    void Track(ComPtr<ID3D12Resource> resource) noexcept;

    void Track(const DescriptorHandle& handle) noexcept;

    // Drain slot[frameIndex]: free queued descriptor slots, then drop ComPtr resource refs.
    // waitIdle=true issues a full CPU/GPU sync first — only needed when there's no fence wait
    // upstream (init phases, explicit Flush()).
    void Cleanup(int frameIndex, bool waitIdle = false) noexcept;
};

#define gfxResourceHandler GfxResourceHandler::Instance()

// =================================================================================================
