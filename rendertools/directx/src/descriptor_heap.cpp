#include "descriptor_heap.h"
#include "dx12context.h"
#include "resource_handler.h"
#include "sampler_cache.h"

#include <cstdio>

// =================================================================================================

bool DescriptorHeap::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool gpuVisible, bool mirrored, uint32_t extraDescriptors) noexcept {
    m_type = type;
    m_capacity = capacity;
    m_count = 0;
    m_gpuVisible = gpuVisible;
    m_owners.Resize(capacity);

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = capacity + extraDescriptors;
    desc.Flags = gpuVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr)) {
        fprintf(stderr, "DescriptorHeap: CreateDescriptorHeap type=%d failed (hr=0x%08X)\n", (int)type, (unsigned)hr);
        return false;
    }
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);

    m_mirror.Reset();
    if (mirrored) {
        D3D12_DESCRIPTOR_HEAP_DESC mirrorDesc{};
        mirrorDesc.Type = type;
        mirrorDesc.NumDescriptors = capacity;
        mirrorDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        mirrorDesc.NodeMask = 0;
        hr = device->CreateDescriptorHeap(&mirrorDesc, IID_PPV_ARGS(&m_mirror));
        if (FAILED(hr)) {
            fprintf(stderr, "DescriptorHeap: CreateDescriptorHeap (mirror) type=%d failed (hr=0x%08X)\n", int(type), unsigned(hr));
            return false;
        }
    }
    return true;
}


D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::CpuHandle(uint32_t index) const noexcept {
    ID3D12DescriptorHeap* heap = m_mirror ? m_mirror.Get() : m_heap.Get();
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(index) * m_descriptorSize;
    return h;
}


D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::HeapCpuHandle(uint32_t index) const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(index) * m_descriptorSize;
    return h;
}


void DescriptorHeap::Publish(uint32_t index) noexcept {
    if (not m_mirror or (index >= m_capacity))
        return;
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return;
    device->CopyDescriptorsSimple(1, HeapCpuHandle(index), CpuHandle(index), m_type);
}


D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GpuHandle(uint32_t index) const noexcept {
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(index) * m_descriptorSize;
    return h;
}


DescriptorHandle DescriptorHeap::Allocate(void) noexcept {
    uint32_t idx;
    if (m_freeList.Length() > 0) {
        idx = m_freeList.Pop();
    } else {
        if (m_count >= m_capacity) {
#ifdef _DEBUG
            fprintf(stderr, "DescriptorHeap: heap full (capacity=%u, type=%d)\n", m_capacity, (int)m_type);
            DumpOwners();
#endif
            return {};
        }
        idx = m_count++;
    }
    DescriptorHandle h;
    h.index = idx;
    h.m_heap = this;
    h.cpuHandle = CpuHandle(idx);
    if (m_gpuVisible)
        h.gpuHandle = GpuHandle(idx);
    return h;
}


void DescriptorHeap::Free(uint32_t index) noexcept {
    if (m_heap and (index < m_count)) {
        m_freeList.Append(index);
        if (index < uint32_t(m_owners.Length()))
            m_owners[index] = {};
    }
}


#if DBG_DIRECTX
void DescriptorHeap::SetOwner(uint32_t index, const std::source_location& loc) noexcept {
    if (index < uint32_t(m_owners.Length()))
        m_owners[index] = loc;
}


void DescriptorHeap::DumpOwners(void) noexcept {
    uint32_t inUse = m_count - uint32_t(m_freeList.Length());
    fprintf(stderr, "--- DescriptorHeap type=%d: %u/%u slots in use ---\n", (int)m_type, inUse, m_capacity);
    for (uint32_t i = 0; i < m_count; ++i) {
        bool isFree = false;
        for (uint32_t j = 0; j < uint32_t(m_freeList.Length()); ++j) {
            if (m_freeList[j] == i) {
                isFree = true;
                break;
            }
        }
        if (isFree)
            continue;
        const std::source_location& loc = m_owners[i];
        fprintf(stderr, "  [%u] %s:%u  %s\n", i, loc.file_name(), (unsigned)loc.line(), loc.function_name());
    }
}
#endif

bool DescriptorHeapHandler::Create(ID3D12Device* device) noexcept {
    if (not m_rtvHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_CAPACITY, false))
        return false;
    if (not m_dsvHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_CAPACITY, false))
        return false;
    if (not m_srvHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SRV_CAPACITY, true, true, TABLE_FRAME_SLOTS * TABLE_CAPACITY))
        return false;
    if (not m_samplerHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SAMPLER_CAPACITY, true))
        return false;

    DescriptorHandle nullTexture = m_srvHeap.Allocate();
    DescriptorHandle nullBuffer = m_srvHeap.Allocate();
    DescriptorHandle nullUav = m_srvHeap.Allocate();
    if (not (nullTexture.IsValid() and nullBuffer.IsValid() and nullUav.IsValid()))
        return false;

    D3D12_SHADER_RESOURCE_VIEW_DESC textureDesc{};
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textureDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textureDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(nullptr, &textureDesc, nullTexture.cpuHandle);
    m_srvHeap.Publish(nullTexture.index);
    m_nullTextureSrv = nullTexture.index;

    D3D12_SHADER_RESOURCE_VIEW_DESC bufferDesc{};
    bufferDesc.Format = DXGI_FORMAT_R32_UINT;
    bufferDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    bufferDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    bufferDesc.Buffer.FirstElement = 0;
    bufferDesc.Buffer.NumElements = 1;
    device->CreateShaderResourceView(nullptr, &bufferDesc, nullBuffer.cpuHandle);
    m_srvHeap.Publish(nullBuffer.index);
    m_nullBufferSrv = nullBuffer.index;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R32_UINT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 1;
    device->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, nullUav.cpuHandle);
    m_srvHeap.Publish(nullUav.index);
    m_nullUav = nullUav.index;

    m_defaultSampler = samplerCache.GetSlot(TextureSampling{});
    if (m_defaultSampler == UINT32_MAX)
        return false;

    ResetTables(0);
    return true;
}


void DescriptorHeapHandler::ResetTables(uint32_t frameIndex) noexcept {
    m_tableFrame = frameIndex % TABLE_FRAME_SLOTS;
    m_tableOffset = 0;
    ++m_tableGeneration;
    m_tableOverflowReported = false;
}


bool DescriptorHeapHandler::BuildTable(const uint32_t* srvIndices, uint32_t count, uint32_t nullIndex, D3D12_GPU_DESCRIPTOR_HANDLE& table) noexcept {
    static constexpr uint32_t kMaxTableSize = 32;

    ID3D12Device* device = dx12Context.Device();
    if (not device or (count == 0) or (count > kMaxTableSize))
        return false;
    if (m_tableOffset + count > TABLE_CAPACITY) {
        if (not m_tableOverflowReported) {
            fprintf(stderr, "DescriptorHeapHandler::BuildTable: descriptor table ring full (%u descriptors per frame)\n", TABLE_CAPACITY);
            m_tableOverflowReported = true;
        }
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE sources[kMaxTableSize];
    UINT sourceSizes[kMaxTableSize];
    for (uint32_t i = 0; i < count; ++i) {
        sources[i] = m_srvHeap.CpuHandle((srvIndices[i] == UINT32_MAX) ? nullIndex : srvIndices[i]);
        sourceSizes[i] = 1;
    }

    uint32_t first = SRV_CAPACITY + m_tableFrame * TABLE_CAPACITY + m_tableOffset;
    D3D12_CPU_DESCRIPTOR_HANDLE destination = m_srvHeap.HeapCpuHandle(first);
    UINT destinationSize = UINT(count);
    device->CopyDescriptors(1, &destination, &destinationSize, UINT(count), sources, sourceSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_tableOffset += count;
    gfxResourceHandler.NoteFrameAllocation();
    table = m_srvHeap.GpuHandle(first);
    return true;
}

// =================================================================================================
