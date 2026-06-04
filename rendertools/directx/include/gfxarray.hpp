#pragma once

#include "dx12framework.h"
#include "dx12context.h"
#include "descriptor_heap.h"
#include "commandlist.h"
#include "resource_handler.h"
#include "shader.h"
#include "array.hpp"
#include "gfxtypes.h"

#include <type_traits>

// =================================================================================================

class BaseGfxArray {
public:
    static inline bool IsAvailable{ true };
};

template <typename DATA_T, typename STORAGE_T = GfxTypes::UavTexture>
class GfxArray : public BaseGfxArray
{
public:
    static constexpr bool isBuffer = std::is_same_v<STORAGE_T, GfxTypes::StructuredBuffer>;

    AutoArray<DATA_T>                   m_data;
    ComPtr<ID3D12Resource>              m_resource;
    ComPtr<ID3D12Resource>              m_upload;
    ComPtr<ID3D12Resource>              m_readback;
    DescriptorHandle                    m_uavHandle;
    ComPtr<ID3D12DescriptorHeap>        m_cpuHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE         m_cpuUavHandle{};
    D3D12_RESOURCE_STATES               m_state{ D3D12_RESOURCE_STATE_COMMON };
    UINT                                m_width{0};
    UINT                                m_height{0};

    GfxArray() = default;

    ~GfxArray() { Destroy(); }

    inline DATA_T* Data(void) { return m_data.Data(); }
    inline int DataSize(void) { return m_data.DataSize(); }

    inline D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle(void) {
        return descriptorHeaps.m_srvHeap.GpuHandle(m_uavHandle.index);
    }

    bool Create(int width, int height = 1) {
        if constexpr (isBuffer)
            return CreateBuffer(width, height);
        else
            return CreateTexture(width, height);
    }

    void Destroy(void) {
        if (m_resource) {
            descriptorHeaps.FreeSRV(m_uavHandle);
            m_uavHandle = {};
            m_cpuHeap.Reset();
            m_cpuUavHandle = {};
            m_resource.Reset();
            m_upload.Reset();
            m_readback.Reset();
        }
        m_data.Reset();
        m_width = m_height = 0;
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

    void Clear([[maybe_unused]] DATA_T value) {
        if constexpr (not isBuffer) {
            if (not m_resource or not m_uavHandle.IsValid())
                return;
            auto* list = commandListHandler.CurrentGfxList();
            if (not list)
                return;
            UINT clearValues[4] = { UINT(value), UINT(value), UINT(value), UINT(value) };
            auto& heap = descriptorHeaps.m_srvHeap;
            list->ClearUnorderedAccessViewUint(heap.GpuHandle(m_uavHandle.index), m_cpuUavHandle, m_resource.Get(), clearValues, 0, nullptr);
            // ClearUnorderedAccessViewUint is not implicitly ordered against subsequent UAV
            // accesses. Without a UAV barrier the clear can run concurrently with or after the
            // following shader pass (e.g. the decal mask's InterlockedMin), corrupting the data.
            // (OpenGL serializes glClearNamedBufferData before SSBO access implicitly; DX12 does not.)
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = m_resource.Get();
            list->ResourceBarrier(1, &b);
        }
    }

    bool Upload(void) {
        if constexpr (isBuffer)
            return UploadBuffer();
        else
            return UploadTexture();
    }

    bool Download(void) {
        if constexpr (isBuffer)
            return DownloadBuffer();
        else
            return DownloadTexture();
    }

    // Upload only [first, first+count) elements (structured-buffer path); leaves the rest of the
    // GPU buffer untouched. Used to spawn one particle system without resetting the others.
    bool UploadRange(int first, int count) {
        if constexpr (isBuffer)
            return UploadBufferRange(first, count);
        else
            return false;
    }

private:
    bool CreateTexture(int width, int height) {
        Destroy();
        m_width = UINT(width);
        m_height = UINT(height);
        int size = width * height;
        m_data.Resize(size);
        ID3D12Device* device = dx12Context.Device();
        if (not device or (size == 0))
            return false;

        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = m_width;
        rd.Height = m_height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R32_UINT;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
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
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R32_UINT;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;
        device->CreateUnorderedAccessView(m_resource.Get(), nullptr, &uavDesc, m_uavHandle.cpuHandle);

        D3D12_DESCRIPTOR_HEAP_DESC cpuHeapDesc{};
        cpuHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cpuHeapDesc.NumDescriptors = 1;
        cpuHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&cpuHeapDesc, IID_PPV_ARGS(&m_cpuHeap)))) {
            descriptorHeaps.FreeSRV(m_uavHandle);
            m_uavHandle = {};
            m_resource.Reset();
            return false;
        }
        m_cpuUavHandle = m_cpuHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateUnorderedAccessView(m_resource.Get(), nullptr, &uavDesc, m_cpuUavHandle);
        return true;
    }

    bool CreateBuffer(int width, int height) {
        Destroy();
        m_width = UINT(width);
        m_height = UINT(height);
        int size = width * height;
        m_data.Resize(size);
        ID3D12Device* device = dx12Context.Device();
        if (not device or (size == 0))
            return false;

        UINT64 byteSize = UINT64(size) * UINT64(sizeof(DATA_T));

        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = byteSize;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // Buffers ignore a non-COMMON InitialState (D3D12 warning 1328) and are created in COMMON.
        // Track that honestly so the first SetBarrier transitions from the real state, not a phantom one.
        m_state = D3D12_RESOURCE_STATE_COMMON;
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
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(size);
        uavDesc.Buffer.StructureByteStride = UINT(sizeof(DATA_T));
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(m_resource.Get(), nullptr, &uavDesc, m_uavHandle.cpuHandle);
        return true;
    }

    bool UploadTexture(void) {
        if (not m_resource or m_data.IsEmpty())
            return false;
        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;

        D3D12_RESOURCE_DESC resDesc = m_resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT numRows;
        UINT64 rowSizeInBytes, totalBytes;
        device->GetCopyableFootprints(&resDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

        if (not m_upload) {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = totalBytes;
            rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_upload))))
                return false;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{ 0, 0 };
        if (FAILED(m_upload->Map(0, &readRange, &mapped)))
            return false;

        UINT srcRowPitch = m_width * sizeof(DATA_T);
        uint8_t* dst = static_cast<uint8_t*>(mapped) + footprint.Offset;
        const uint8_t* src = reinterpret_cast<const uint8_t*>(m_data.Data());
        for (UINT row = 0; row < numRows; ++row)
            std::memcpy(dst + row * footprint.Footprint.RowPitch, src + row * srcRowPitch, srcRowPitch);
        m_upload->Unmap(0, nullptr);

        auto* list = commandListHandler.CurrentGfxList();
        if (not list)
            return false;
        SetBarrier(list, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = m_resource.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = m_upload.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprint;

        list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
        SetBarrier(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }

    bool UploadBuffer(void) {
        if (not m_resource or m_data.IsEmpty())
            return false;
        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;

        size_t byteSize = size_t(DataSize());

        if (not m_upload) {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = byteSize;
            rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_upload))))
                return false;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{ 0, 0 };
        if (FAILED(m_upload->Map(0, &readRange, &mapped)))
            return false;
        std::memcpy(mapped, m_data.Data(), byteSize);
        m_upload->Unmap(0, nullptr);

        auto* list = commandListHandler.CurrentGfxList();
        if (not list)
            return false;
        SetBarrier(list, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(m_resource.Get(), 0, m_upload.Get(), 0, byteSize);
        SetBarrier(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }

    bool UploadBufferRange(int first, int count) {
        if (not m_resource or m_data.IsEmpty() or (count <= 0))
            return false;
        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;

        size_t elemSize = sizeof(DATA_T);
        size_t fullSize = size_t(DataSize());
        size_t offset = size_t(first) * elemSize;
        size_t bytes = size_t(count) * elemSize;
        if (offset + bytes > fullSize)
            return false;

        if (not m_upload) {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = fullSize;
            rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_upload))))
                return false;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{ 0, 0 };
        if (FAILED(m_upload->Map(0, &readRange, &mapped)))
            return false;
        std::memcpy(static_cast<uint8_t*>(mapped) + offset, reinterpret_cast<const uint8_t*>(m_data.Data()) + offset, bytes);
        m_upload->Unmap(0, nullptr);

        auto* list = commandListHandler.CurrentGfxList();
        if (not list)
            return false;
        SetBarrier(list, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(m_resource.Get(), offset, m_upload.Get(), offset, bytes);
        SetBarrier(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }

    bool DownloadTexture(void) {
        if (not m_resource or m_data.IsEmpty())
            return false;
        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;

        D3D12_RESOURCE_DESC resDesc = m_resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT numRows;
        UINT64 rowSizeInBytes, totalBytes;
        device->GetCopyableFootprints(&resDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

        if (not m_readback) {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_READBACK };
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = totalBytes;
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

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = m_resource.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = m_readback.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint = footprint;

        list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
        SetBarrier(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        void* mapped = nullptr;
        D3D12_RANGE readRange{ 0, SIZE_T(totalBytes) };
        if (FAILED(m_readback->Map(0, &readRange, &mapped)))
            return false;

        UINT dstRowPitch = m_width * sizeof(DATA_T);
        const uint8_t* src = static_cast<const uint8_t*>(mapped) + footprint.Offset;
        uint8_t* dst = reinterpret_cast<uint8_t*>(m_data.Data());
        for (UINT row = 0; row < numRows; ++row)
            std::memcpy(dst + row * dstRowPitch, src + row * footprint.Footprint.RowPitch, dstRowPitch);

        D3D12_RANGE writeRange{ 0, 0 };
        m_readback->Unmap(0, &writeRange);
        return true;
    }

    bool DownloadBuffer(void) {
        if (not m_resource or m_data.IsEmpty())
            return false;
        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;

        size_t byteSize = size_t(DataSize());

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
        std::memcpy(m_data.Data(), mapped, byteSize);
        D3D12_RANGE writeRange{ 0, 0 };
        m_readback->Unmap(0, &writeRange);
        return true;
    }
};

// =================================================================================================
