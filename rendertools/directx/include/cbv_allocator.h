#pragma once

#include "framework.h"
#include "basesingleton.hpp"

// =================================================================================================
// CbvLinearAllocator — per-frame linear allocator for constant buffer sub-allocations.
//
// Solves the upload-heap aliasing problem: a single ID3D12Resource per Shader was overwritten
// by later draws in the same frame, causing all draws of the same shader to read the last
// CPU-written value rather than the value at record time.
//
// Usage:
//   BeginFrame / Open  →  cbvAllocator.Reset(frameIndex)   (resets the offset to 0)
//   UpdateMatrices()   →  auto a = cbvAllocator.Allocate(sizeof(FrameConstants))
//                         memcpy(a.cpu, staging, size)
//                         list->SetGraphicsRootConstantBufferView(0, a.gpu)
//   UploadB1()         →  same pattern with root slot 1

struct CbAlloc {
    uint8_t*                  cpu{ nullptr };
    D3D12_GPU_VIRTUAL_ADDRESS gpu{ 0 };

    bool IsValid(void) const noexcept { return cpu != nullptr; }
};

class CbvLinearAllocator : public BaseSingleton<CbvLinearAllocator>
{
    static constexpr UINT kAlign       = 256;
    static constexpr UINT kInitCap     = 512u * 1024u;   // 64 KB = 256 slots per frame
    static constexpr UINT kMaxCap      = 1024u * 1024u; // 1 MB hard ceiling

    struct FrameData {
        ComPtr<ID3D12Resource>    resource;
        uint8_t*                  cpuBase    = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuBase    = 0;
        UINT                      offset     = 0;
        UINT                      capacity   = 0;
        UINT                      peakOffset = 0;
    };

    FrameData  m_frames[2];   // FRAME_COUNT = 2
    UINT       m_frameIndex{ 0 };

    bool AllocFrame(ID3D12Device* device, UINT frameIdx, UINT capacity) noexcept;

public:
    bool Create(ID3D12Device* device) noexcept;
    void Destroy(void) noexcept;

    // Reset at the start of a frame (call after fence wait so GPU is done with this frame's data).
    void Reset(UINT frameIndex) noexcept;

    // Allocate 'bytes' (rounded up to 256) from the current frame's buffer.
    // Returns {nullptr, 0} on overflow (logged to stderr).
    CbAlloc Allocate(UINT bytes) noexcept;
};

#define cbvAllocator CbvLinearAllocator::Instance()

// =================================================================================================
