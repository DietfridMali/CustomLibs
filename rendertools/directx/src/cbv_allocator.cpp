#include "cbv_allocator.h"
#include "commandlist.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// =================================================================================================

bool CbvLinearAllocator::CreateBuffer(ID3D12Device* device, UINT capacity, const char* name, ComPtr<ID3D12Resource>& resource, uint8_t*& cpuBase) noexcept
{
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = capacity;
    rd.Height             = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
        fprintf(stderr, "%s: CreateCommittedResource failed (cap=%u, hr=0x%08X)\n", name, capacity, (unsigned)hr);
        return false;
    }
#if DBG_DIRECTX
    resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif

    D3D12_RANGE readRange{ 0, 0 };
    hr = resource->Map(0, &readRange, reinterpret_cast<void**>(&cpuBase));
    if (FAILED(hr)) {
        fprintf(stderr, "%s: Map failed (hr=0x%08X)\n", name, (unsigned)hr);
        resource.Reset();
        cpuBase = nullptr;
        return false;
    }
    return true;
}


bool CbvLinearAllocator::AllocFrame(ID3D12Device* device, UINT frameIdx, UINT capacity) noexcept
{
    auto& f = m_frames[frameIdx];

    if (f.cpuBase) {
        f.resource->Unmap(0, nullptr);
        f.cpuBase = nullptr;
    }
    f.resource.Reset();
    f.gpuBase  = 0;
    f.offset   = 0;
    f.capacity = 0;

    char name[64];
    snprintf(name, sizeof(name), "CbvLinearAllocator[%u]", frameIdx);
    if (not CreateBuffer(device, capacity, name, f.resource, f.cpuBase))
        return false;

    f.gpuBase  = f.resource->GetGPUVirtualAddress();
    f.offset   = 0;
    f.capacity = capacity;
    return true;
}


bool CbvLinearAllocator::AddChunk(FrameData& f, UINT capacity) noexcept
{
    Chunk chunk;
    char name[64];
    snprintf(name, sizeof(name), "CbvLinearAllocator[%u] chained %u", m_frameIndex, UINT(f.overflow.size()));
    if (not CreateBuffer(m_device.Get(), capacity, name, chunk.resource, chunk.cpuBase))
        return false;
    chunk.gpuBase  = chunk.resource->GetGPUVirtualAddress();
    chunk.capacity = capacity;
    f.overflow.push_back(chunk);
    f.overflowOffset = 0;
    return true;
}


void CbvLinearAllocator::DestroyChunks(FrameData& f) noexcept
{
    for (auto& chunk : f.overflow) {
        if (chunk.cpuBase)
            chunk.resource->Unmap(0, nullptr);
        chunk.resource.Reset();
    }
    f.overflow.clear();
    f.overflowOffset = 0;
}


bool CbvLinearAllocator::Create(ID3D12Device* device) noexcept
{
    m_device = device;
    for (UINT i = 0; i < 2; ++i) {
        if (not AllocFrame(device, i, kInitCap))
            return false;
    }
    return true;
}


void CbvLinearAllocator::Destroy(void) noexcept
{
    for (auto& f : m_frames) {
        DestroyChunks(f);
        if (f.cpuBase) {
            f.resource->Unmap(0, nullptr);
            f.cpuBase = nullptr;
        }
        f.resource.Reset();
        f.gpuBase  = 0;
        f.offset   = 0;
        f.capacity = 0;
    }
    m_device.Reset();
}


void CbvLinearAllocator::Reset(UINT frameIndex) noexcept
{
    m_frameIndex = frameIndex;
    auto& f = m_frames[frameIndex];
    DestroyChunks(f);
    if ((f.peakOffset > f.capacity) and (f.capacity < kMaxCap)) {
        UINT newCap = f.capacity;
        while (newCap < f.peakOffset and newCap < kMaxCap)
            newCap *= 2;
        newCap = std::min(newCap, kMaxCap);
        AllocFrame(m_device.Get(), frameIndex, newCap);
    }
    f.peakOffset = 0;
    f.offset = 0;
}


CbAlloc CbvLinearAllocator::Allocate(UINT bytes) noexcept
{
    const UINT aligned = (bytes + kAlign - 1u) & ~(kAlign - 1u);
    auto& f = m_frames[m_frameIndex];

    CbAlloc a;

    if (f.overflow.empty() and (f.offset + aligned <= f.capacity)) {
        a.cpu    = f.cpuBase + f.offset;
        a.gpu    = f.gpuBase + f.offset;
        f.offset += aligned;
        f.peakOffset += aligned;
        return a;
    }

    if (f.overflow.empty() or (f.overflowOffset + aligned > f.overflow.back().capacity)) {
        if (not AddChunk(f, std::max(f.capacity, aligned)))
            return {};
    }

    Chunk& chunk = f.overflow.back();

    a.cpu    = chunk.cpuBase + f.overflowOffset;
    a.gpu    = chunk.gpuBase + f.overflowOffset;
    f.overflowOffset += aligned;
    f.peakOffset += aligned;
    return a;
}

// =================================================================================================
