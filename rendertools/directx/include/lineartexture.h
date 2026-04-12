#pragma once

#ifndef _TEXTURE_H
#   include "texture.h"
#endif

#include "dx12context.h"
#include "command_queue.h"
#include "descriptor_heap.h"
#include <cstdint>
#include <cstring>
#include <algorithm>

// =================================================================================================
// DX12 LinearTexture — 1D data stored as a Texture2D with height=1.
// Replaces the OGL version (glTexImage2D / glTexSubImage2D).

template<typename T> struct GfxTexTraits;

template<> struct GfxTexTraits<uint8_t> {
    static constexpr DXGI_FORMAT format    = DXGI_FORMAT_R8_UNORM;
    static constexpr uint32_t    pixelSize = 1;   // bytes per texel
    static constexpr uint32_t    channels  = 1;
};

template<> struct GfxTexTraits<Vector4f> {
    static constexpr DXGI_FORMAT format    = DXGI_FORMAT_R32G32B32A32_FLOAT;
    static constexpr uint32_t    pixelSize = 16;  // bytes per texel
    static constexpr uint32_t    channels  = 4;
};

// =================================================================================================

template <typename DATA_T>
class LinearTexture : public Texture
{
public:
    AutoArray<DATA_T>   m_data;
    bool                m_isRepeating{ false };

    LinearTexture() = default;
    ~LinearTexture() = default;

    // No per-texture sampler state in DX12 — handled by static samplers in the root signature.
    void SetParams(bool /*enforce*/) override { m_hasParams = true; }


    virtual bool Deploy(int /*bufferIndex*/) override {
        if (m_buffers.IsEmpty()) return false;
        TextureBuffer* tb = m_buffers[0];
        int width = tb->m_info.m_width;
        if (width <= 0) return false;

        ID3D12Device* device = dx12Context.Device();
        if (!device) return false;

        // (Re-)create a Texture2D with height=1
        m_resource.Reset();
        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = UINT(width);
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = GfxTexTraits<DATA_T>::format;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_resource));
        if (FAILED(hr)) return false;

        // Upload the data row
        UINT64 uploadSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        UINT rowCount = 0; UINT64 rowSize = 0;
        device->GetCopyableFootprints(&rd, 0, 1, 0, &layout, &rowCount, &rowSize, &uploadSize);

        D3D12_HEAP_PROPERTIES uhp{ D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC upDesc{};
        upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upDesc.Width     = uploadSize;
        upDesc.Height    = upDesc.DepthOrArraySize = upDesc.MipLevels = 1;
        upDesc.SampleDesc.Count = 1;
        upDesc.Layout    = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upload;
        if (FAILED(device->CreateCommittedResource(&uhp, D3D12_HEAP_FLAG_NONE, &upDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
            return false;

        const uint8_t* src = static_cast<const uint8_t*>(tb->DataBuffer());
        uint8_t* mapped = nullptr;
        D3D12_RANGE mapRange{ 0, 0 };
        if (FAILED(upload->Map(0, &mapRange, (void**)&mapped))) return false;
        std::memcpy(mapped + layout.Offset, src, UINT(width) * GfxTexTraits<DATA_T>::pixelSize);
        upload->Unmap(0, nullptr);

        auto* list = cmdQueue.List();
        if (!list) return false;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{}, dstLoc{};
        srcLoc.pResource       = upload.Get();
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = layout;
        dstLoc.pResource       = m_resource.Get();
        dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;
        list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = m_resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);

        cmdQueue.Execute();
        cmdQueue.WaitIdle();

        // Create / update SRV
        if (m_handle == UINT32_MAX) {
            DescriptorHandle hdl = descriptorHeaps.AllocSRV();
            if (!hdl.IsValid()) return false;
            m_handle = hdl.index;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                    = GfxTexTraits<DATA_T>::format;
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = descriptorHeaps.m_srvHeap.CpuHandle(m_handle);
        device->CreateShaderResourceView(m_resource.Get(), &srvDesc, cpuHandle);

        m_isValid   = true;
        m_hasBuffer = true;
        return true;
    }


    inline bool Create(AutoArray<DATA_T>& data, bool isRepeating) {
        if (!Texture::Create()) return false;
        m_isRepeating = isRepeating;
        if (!Allocate(static_cast<int>(data.Length()))) return false;
        Upload(data);
        return true;
    }


    bool Allocate(int length) {
        TextureBuffer* tb = new (std::nothrow) TextureBuffer();
        if (!tb) return false;
        if (!m_buffers.Append(tb)) { delete tb; return false; }
        tb->m_info = TextureBuffer::BufferInfo(
            length, 1,
            int(GfxTexTraits<DATA_T>::channels),
            0, 0);  // internalFormat/format not used in DX12
        tb->m_data.Resize(length * int(GfxTexTraits<DATA_T>::pixelSize));
        HasBuffer() = true;
        Deploy(0);
        return true;
    }


    inline int Upload(AutoArray<DATA_T>& data) {
        if (m_buffers.IsEmpty()) return 0;
        TextureBuffer* tb = m_buffers[0];
        const int l = std::min(GetWidth(), int(data.Length()));
        if (l <= 0) return 0;

        // Update system-memory copy
        std::memcpy(tb->DataBuffer(), data.Data(), l * int(GfxTexTraits<DATA_T>::pixelSize));

        // Re-upload to GPU
        Deploy(0);
        return l;
    }
};

// =================================================================================================
