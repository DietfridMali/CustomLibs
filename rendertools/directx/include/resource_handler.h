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
    using ResourceArray = AutoArray<ComPtr<ID3D12Pageable>>;
    using DescriptorArray      = AutoArray<DescriptorHandle>;

private:
    using SerialArray = AutoArray<uint64_t>;
    using UploadPool = AutoArray<ComPtr<ID3D12Resource>>;

    // Upload heap buffers are handed out from a pool instead of being created and destroyed per use.
    // Every buffer of a size class has the size class' size, so any of them serves any request of
    // that class. D3D12 has no suballocator of its own - each committed resource is a driver
    // allocation, and creating and releasing hundreds of them per frame costs milliseconds (the
    // Vulkan backend gets this for free from VMA).
    static constexpr int        kUploadBuckets = 32;
    static constexpr int        kSmallestUploadBucket = 8;              // 256 bytes
    static constexpr size_t     kUploadPoolLimit = size_t(512) << 20;   // bytes held in the pool

    UploadPool                  m_uploadPool[kUploadBuckets];
    size_t                      m_uploadPoolBytes{ 0 };
    uint32_t                    m_uploadsCreated{ 0 };
    uint32_t                    m_uploadsReused{ 0 };
    uint32_t                    m_resourcesReleased{ 0 };

    static int UploadBucket(size_t size) noexcept;

    bool Recycle(ComPtr<ID3D12Pageable>& resource) noexcept;

    AutoArray<ResourceArray>    m_frameResources;
    AutoArray<ResourceArray>    m_frameUploads;
    AutoArray<DescriptorArray>         m_frameDescriptors;
    AutoArray<SerialArray>      m_frameResourceSerials;
    AutoArray<SerialArray>      m_frameUploadSerials;
    AutoArray<SerialArray>      m_frameDescriptorSerials;
    uint64_t                    m_serial{ 0 };
    uint64_t                    m_lastAllocSerial{ 0 };

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

    void ReleaseUploadPool(void) noexcept;

    ComPtr<ID3D12Resource> GetUploadResource(const char* name, size_t dataSize);

    // An upload heap buffer of at least this size, from the pool when one is free. Not tracked -
    // the caller owns it and hands it back by tracking it for deferred release.
    ComPtr<ID3D12Resource> AcquireUpload(size_t size) noexcept;

    void Track(ComPtr<ID3D12Pageable> resource) noexcept;

    // Hands an upload buffer BACK to the pool once the frames that may still read it are through.
    // The caller gives up ownership here - unlike Track (), which only drops one reference and leaves
    // the resource to whoever else holds it.
    void TrackUpload(ComPtr<ID3D12Resource> resource) noexcept;

    void Track(const DescriptorHandle& handle) noexcept;

    // Drain slot[frameIndex]: free queued descriptor slots, then drop ComPtr resource refs.
    // waitIdle=true issues a full CPU/GPU sync first — only needed when there's no fence wait
    // upstream (init phases, explicit Flush()).
    void Cleanup(int frameIndex, bool waitIdle = false) noexcept;

    void CleanupBefore(int frameIndex, uint64_t serialLimit) noexcept;

    inline uint64_t NextSerial(void) noexcept {
        return ++m_serial;
    }

    inline void NoteFrameAllocation(void) noexcept {
        m_lastAllocSerial = ++m_serial;
    }

    inline uint64_t LastAllocSerial(void) const noexcept {
        return m_lastAllocSerial;
    }
};

#define gfxResourceHandler GfxResourceHandler::Instance()

// =================================================================================================
