#include "cbv_allocator.h"
#include "commandlist.h"

#include <cstdio>
#include <cstring>

// =================================================================================================

bool CbvLinearAllocator::AllocFrame(ID3D12Device* device, UINT frameIdx, UINT capacity) noexcept
{
    auto& f = m_frames[frameIdx];

    if (f.cpuBase) {
        f.resource->Unmap(0, nullptr);
        f.cpuBase = nullptr;
    }
    f.resource.Reset();

    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = capacity;
    rd.Height             = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&f.resource));
    if (FAILED(hr)) {
        fprintf(stderr, "CbvLinearAllocator: CreateCommittedResource[%u] failed (hr=0x%08X)\n",
                frameIdx, (unsigned)hr);
        return false;
    }

    D3D12_RANGE readRange{ 0, 0 };
    hr = f.resource->Map(0, &readRange, reinterpret_cast<void**>(&f.cpuBase));
    if (FAILED(hr)) {
        fprintf(stderr, "CbvLinearAllocator: Map[%u] failed (hr=0x%08X)\n", frameIdx, (unsigned)hr);
        f.resource.Reset();
        return false;
    }

    f.gpuBase  = f.resource->GetGPUVirtualAddress();
    f.offset   = 0;
    f.capacity = capacity;
    return true;
}


bool CbvLinearAllocator::Create(ID3D12Device* device) noexcept
{
    for (UINT i = 0; i < 2; ++i) {
        if (not AllocFrame(device, i, kInitCap))
            return false;
    }
    return true;
}


void CbvLinearAllocator::Destroy(void) noexcept
{
    for (auto& f : m_frames) {
        if (f.cpuBase) {
            f.resource->Unmap(0, nullptr);
            f.cpuBase = nullptr;
        }
        f.resource.Reset();
        f.gpuBase  = 0;
        f.offset   = 0;
        f.capacity = 0;
    }
}


void CbvLinearAllocator::Reset(UINT frameIndex) noexcept
{
    m_frameIndex = frameIndex;
    m_frames[frameIndex].offset = 0;
}


CbAlloc CbvLinearAllocator::Allocate(UINT bytes) noexcept
{
    const UINT aligned = (bytes + kAlign - 1u) & ~(kAlign - 1u);
    auto& f = m_frames[m_frameIndex];

    if (f.offset + aligned > f.capacity) {
        // Grow: double capacity up to kMaxCap.
        const UINT newCap = (f.capacity * 2u < kMaxCap) ? f.capacity * 2u : kMaxCap;
        if (f.offset + aligned > newCap) {
            fprintf(stderr, "CbvLinearAllocator: frame %u exhausted (capacity %u)\n",
                    m_frameIndex, newCap);
            return {};
        }
        // Realloc requires device — retrieve it from the existing resource's device.
        ComPtr<ID3D12Device> device;
        f.resource->GetDevice(IID_PPV_ARGS(&device));
        if (not device or not AllocFrame(device.Get(), m_frameIndex, newCap))
            return {};
    }

    CbAlloc a;
    a.cpu    = f.cpuBase + f.offset;
    a.gpu    = f.gpuBase + f.offset;
    f.offset += aligned;
    return a;
}

// =================================================================================================
