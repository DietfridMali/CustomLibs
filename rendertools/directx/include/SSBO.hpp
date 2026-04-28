#pragma once

#include "dx12framework.h"
#include "dx12context.h"
#include "descriptor_heap.h"
#include "commandlist.h"
#include "resource_handler.h"
#include "shader.h"
#include "array.hpp"

// =================================================================================================

class BaseSSBO {
public:
    static inline bool IsAvailable{ true };
};

template <typename DATA_T>
class SSBO : public BaseSSBO
{
public:
    AutoArray<DATA_T>       m_data;
    ComPtr<ID3D12Resource>  m_resource;
    ComPtr<ID3D12Resource>  m_upload;
    ComPtr<ID3D12Resource>  m_readback;
    DescriptorHandle        m_uavHandle;
    D3D12_RESOURCE_STATES   m_state{ D3D12_RESOURCE_STATE_COMMON };

    SSBO() = default;

    ~SSBO() { Destroy(); }

    inline DATA_T* Data(void) { return m_data.Data(); }
    inline int DataSize(void) { return m_data.DataSize(); }

    bool Create(int size = 0) {
        Destroy();
        m_data.Resize(size);
        ID3D12Device* device = dx12Context.Device();
        if (not device or (size == 0))
            return false;

        UINT64 byteSize = UINT64(size) * sizeof(DATA_T);

        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = byteSize;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        m_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, m_state, nullptr, IID_PPV_ARGS(&m_resource))))
            return false;

        m_uavHandle = descriptorHeaps.AllocSRV();
        if (not m_uavHandle.IsValid()) {
            m_resource.Reset();
            return false;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.NumElements = UINT(size);
        uavDesc.Buffer.StructureByteStride = sizeof(DATA_T);
        device->CreateUnorderedAccessView(m_resource.Get(), nullptr, &uavDesc, m_uavHandle.cpu);
        return true;
    }

    void Destroy(void) {
        if (m_resource) {
            descriptorHeaps.FreeSRV(m_uavHandle);
            m_uavHandle = {};
            m_resource.Reset();
            m_upload.Reset();
            m_readback.Reset();
        }
        m_data.Destroy();
    }

    void SetBarrier(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES after) {
        if (list and m_resource and (m_state != after)) {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = m_resource.Get();
            b.Transition.StateBefore = m_state;
            b.Transition.StateAfter = after;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &b);
            m_state = after;
        }
    }

    bool Bind(uint32_t bindingPoint) {
        if (not m_resource or not m_uavHandle.IsValid())
            return false;
        auto* list = commandListHandler.CurrentGfxList();
        if (not list)
            return false;
        auto& heap = descriptorHeaps.m_srvHeap;
        if (heap.m_heap)
            list->SetGraphicsRootDescriptorTable(UINT(Shader::kUavBase + bindingPoint), heap.GpuHandle(m_uavHandle.index));
        return true;
    }

    void Release(uint32_t /*bindingPoint*/) {}

    void Clear(DATA_T value) {
        if (not m_resource or not m_uavHandle.IsValid())
            return;
        auto* list = commandListHandler.CurrentGfxList();
        if (not list)
            return;
        UINT clearValues[4] = { UINT(value), UINT(value), UINT(value), UINT(value) };
        auto& heap = descriptorHeaps.m_srvHeap;
        list->ClearUnorderedAccessViewUint(heap.GpuHandle(m_uavHandle.index), m_uavHandle.cpu, m_resource.Get(), clearValues, 0, nullptr);
    }

    bool Upload(void) {
        if (not m_resource or m_data.IsEmpty())
            return false;
        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;
        UINT64 byteSize = UINT64(m_data.Length()) * sizeof(DATA_T);
        if (not m_upload) {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = byteSize;
            rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_upload))))
                return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE readRange{ 0, 0 };
        if (FAILED(m_upload->Map(0, &readRange, &mapped)))
            return false;
        std::memcpy(mapped, m_data.Data(), size_t(byteSize));
        m_upload->Unmap(0, nullptr);

        auto* list = commandListHandler.CurrentGfxList();
        if (not list)
            return false;
        SetBarrier(list, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(m_resource.Get(), 0, m_upload.Get(), 0, byteSize);
        SetBarrier(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }

    bool Download(void) {
        if (not m_resource or m_data.IsEmpty())
            return false;
        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;
        UINT64 byteSize = UINT64(m_data.Length()) * sizeof(DATA_T);
        if (not m_readback) {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_READBACK };
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = byteSize;
            rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readback))))
                return false;
        }
        auto* list = commandListHandler.CurrentGfxList();
        if (not list)
            return false;
        SetBarrier(list, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(m_readback.Get(), 0, m_resource.Get(), 0, byteSize);
        SetBarrier(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        void* mapped = nullptr;
        D3D12_RANGE readRange{ 0, SIZE_T(byteSize) };
        if (FAILED(m_readback->Map(0, &readRange, &mapped)))
            return false;
        std::memcpy(m_data.Data(), mapped, size_t(byteSize));
        D3D12_RANGE writeRange{ 0, 0 };
        m_readback->Unmap(0, &writeRange);
        return true;
    }
};

// =================================================================================================
