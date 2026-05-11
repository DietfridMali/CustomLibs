#include "descriptor_heap.h"

#include <cstdio>

// =================================================================================================

bool DescriptorHeap::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool gpuVisible) noexcept {
    m_type = type;
    m_capacity = capacity;
    m_count = 0;
    m_gpuVisible = gpuVisible;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = capacity;
    desc.Flags = gpuVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr)) {
        fprintf(stderr, "DescriptorHeap: CreateDescriptorHeap type=%d failed (hr=0x%08X)\n",
                (int)type, (unsigned)hr);
        return false;
    }
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
    return true;
}


D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::CpuHandle(UINT index) const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(index) * m_descriptorSize;
    return h;
}


D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GpuHandle(UINT index) const noexcept {
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(index) * m_descriptorSize;
    return h;
}


DescriptorHandle DescriptorHeap::Allocate(void) noexcept {
    UINT idx;
    if (m_freeList.Length() > 0) {
        idx = m_freeList.Pop();
    } else {
        if (m_count >= m_capacity) {
            fprintf(stderr, "DescriptorHeap: heap full (capacity=%u, type=%d)\n",
                    m_capacity, (int)m_type);
            return {};
        }
        idx = m_count++;
    }
    DescriptorHandle h;
    h.index = idx;
    h.cpuHandle = CpuHandle(idx);
    if (m_gpuVisible)
        h.gpuHandle = GpuHandle(idx);
    return h;
}


void DescriptorHeap::Free(UINT index) noexcept {
    if (m_heap and (index < m_count))
        m_freeList.Append(index);
}


bool DescriptorHeapHandler::Create(ID3D12Device* device) noexcept {
    if (not m_rtvHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_CAPACITY, false))
        return false;
    if (not m_dsvHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_CAPACITY, false))
        return false;
    if (not m_srvHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SRV_CAPACITY, true))
        return false;
    if (not m_samplerHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SAMPLER_CAPACITY, true))
        return false;
    return true;
}

// =================================================================================================
