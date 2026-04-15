#pragma once

#ifndef _TEXTURE_H
#   include "texture.h"
#endif

#include "dx12context.h"
#include "dx12upload.h"
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
        if (m_buffers.IsEmpty())
            return false;
        TextureBuffer* tb = m_buffers[0];
        int width = tb->m_info.m_width;
        if (width <= 0)
            return false;

        ID3D12Device* device = dx12Context.Device();
        if (not device)
            return false;

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

        if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_resource))))
            return false;

        const uint8_t* src = static_cast<const uint8_t*>(tb->DataBuffer());
        if (not UploadTextureData(device, m_resource.Get(), src, width, 1, int(GfxTexTraits<DATA_T>::pixelSize)))
            return false;

        // Create / update SRV
        if (m_handle == UINT32_MAX) {
            DescriptorHandle hdl = descriptorHeaps.AllocSRV();
            if (not hdl.IsValid())
                return false;
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
        if (not Texture::Create())
            return false;
        m_isRepeating = isRepeating;
        if (not Allocate(static_cast<int>(data.Length())))
            return false;
        Upload(data);
        return true;
    }


    bool Allocate(int length) {
        TextureBuffer* tb = new (std::nothrow) TextureBuffer();
        if (not tb)
            return false;
        if (not m_buffers.Append(tb)) {
            delete tb;
            return false;
        }
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
