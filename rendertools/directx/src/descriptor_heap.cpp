#include "descriptor_heap.h"
#include "dx12context.h"
#include "resource_handler.h"
#include "sampler_cache.h"
#include "dx12upload.h"
#include "commandlist.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

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
    commandListHandler.InvalidateTables();
}


bool DescriptorHeap::Grow(ID3D12Device* device, uint32_t extraDescriptors) noexcept {
    if (not (device and m_gpuVisible and m_mirror))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = m_type;
    desc.NumDescriptors = m_capacity + extraDescriptors;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;

    ComPtr<ID3D12DescriptorHeap> heap;
    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr)) {
        fprintf(stderr, "DescriptorHeap::Grow: CreateDescriptorHeap type=%d, %u descriptors failed (hr=0x%08X)\n", int(m_type), desc.NumDescriptors, unsigned(hr));
        return false;
    }
    if (m_count > 0)
        device->CopyDescriptorsSimple(m_count, heap->GetCPUDescriptorHandleForHeapStart(), m_mirror->GetCPUDescriptorHandleForHeapStart(), m_type);
    gfxResourceHandler.Track(m_heap);
    m_heap = std::move(heap);
    return true;
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
    if (not m_srvHeap.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SRV_CAPACITY, true, true, TABLE_FRAME_SLOTS * m_tableCapacity))
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


static ComPtr<ID3D12Resource> CreateDefaultTexture(ID3D12Device* device, UINT16 layers, [[maybe_unused]] const char* name) noexcept {
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = 1;
    rd.Height = 1;
    rd.DepthOrArraySize = layers;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource))))
        return nullptr;
#if DBG_DIRECTX
    resource->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(strlen(name)), name);
#endif
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    const uint8_t* faces[6] = { white, white, white, white, white, white };
    if (not UploadTextureData(device, resource.Get(), faces, int(layers), 1, 1, 4))
        return nullptr;
    return resource;
}


bool DescriptorHeapHandler::CreateDefaultTextures(ID3D12Device* device) noexcept {
    static const uint8_t white[4] = { 255, 255, 255, 255 };

    m_defaultFlat = CreateDefaultTexture(device, 1, "DefaultTexture 2D");
    m_defaultCube = CreateDefaultTexture(device, 6, "DefaultTexture Cube");
    m_defaultVolume = Upload3DTextureData(device, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 4, white);
    if (not (m_defaultFlat and m_defaultCube and m_defaultVolume)) {
        fprintf(stderr, "DescriptorHeapHandler: default textures could not be created\n");
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC descs[kDefaultViewTypes]{};
    ID3D12Resource* resources[kDefaultViewTypes] = { nullptr, m_defaultFlat.Get(), m_defaultFlat.Get(), m_defaultCube.Get(), m_defaultVolume.Get() };

    for (int i = 1; i < kDefaultViewTypes; ++i) {
        descs[i].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        descs[i].Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    }
    descs[1].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    descs[1].Texture2D.MipLevels = 1;
    descs[2].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    descs[2].Texture2DArray.MipLevels = 1;
    descs[2].Texture2DArray.ArraySize = 1;
    descs[3].ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    descs[3].TextureCube.MipLevels = 1;
    descs[4].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    descs[4].Texture3D.MipLevels = 1;

    for (int i = 1; i < kDefaultViewTypes; ++i) {
        DescriptorHandle handle = m_srvHeap.Allocate();
        if (not handle.IsValid())
            return false;
        device->CreateShaderResourceView(resources[i], &descs[i], handle.cpuHandle);
        m_srvHeap.Publish(handle.index);
        m_defaultSrvs[i] = handle.index;
    }
    return true;
}


void DescriptorHeapHandler::ResetTables(uint32_t frameIndex) noexcept {
    m_tableFrame = frameIndex % TABLE_FRAME_SLOTS;
    m_tableOffset = 0;
    ++m_tableGeneration;
    m_tableOverflowReported = false;
}


bool DescriptorHeapHandler::GrowTables(ID3D12Device* device) noexcept {
    const uint32_t maxCapacity = (MAX_SHADER_VISIBLE_DESCRIPTORS - SRV_CAPACITY) / TABLE_FRAME_SLOTS;

    if (m_tableCapacity >= maxCapacity)
        return false;

    uint32_t capacity = std::min(m_tableCapacity * 2, maxCapacity);

    if (not m_srvHeap.Grow(device, TABLE_FRAME_SLOTS * capacity))
        return false;
    fprintf(stderr, "DescriptorHeapHandler: descriptor table ring grown from %u to %u descriptors per frame\n", m_tableCapacity, capacity);
    m_tableCapacity = capacity;
    m_tableOffset = 0;
    ++m_tableGeneration;
    ++m_heapVersion;
    commandListHandler.OnDescriptorHeapChanged();
    return true;
}


bool DescriptorHeapHandler::BuildTable(const uint32_t* srvIndices, uint32_t count, uint32_t nullIndex, D3D12_GPU_DESCRIPTOR_HANDLE& table) noexcept {
    static constexpr uint32_t kMaxTableSize = 32;

    ID3D12Device* device = dx12Context.Device();
    if (not device or (count == 0) or (count > kMaxTableSize))
        return false;
    if ((m_tableOffset + count > m_tableCapacity) and not GrowTables(device)) {
        if (not m_tableOverflowReported) {
            fprintf(stderr, "DescriptorHeapHandler::BuildTable: descriptor table ring full (%u descriptors per frame)\n", m_tableCapacity);
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

    uint32_t first = SRV_CAPACITY + m_tableFrame * m_tableCapacity + m_tableOffset;
    D3D12_CPU_DESCRIPTOR_HANDLE destination = m_srvHeap.HeapCpuHandle(first);
    UINT destinationSize = UINT(count);
    device->CopyDescriptors(1, &destination, &destinationSize, UINT(count), sources, sourceSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_tableOffset += count;
    gfxResourceHandler.NoteFrameAllocation();
    table = m_srvHeap.GpuHandle(first);
    return true;
}

// =================================================================================================
