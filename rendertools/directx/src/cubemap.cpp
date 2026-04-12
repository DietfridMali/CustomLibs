#define NOMINMAX

#include "cubemap.h"
#include "texturebuffer.h"
#include "descriptor_heap.h"
#include "command_queue.h"
#include "dx12context.h"

// =================================================================================================
// DX12 Cubemap implementation

void Cubemap::SetParams(void) {
    // Sampling parameters are baked into the root-signature static samplers.
    m_hasParams = true;
}


bool Cubemap::Deploy(int /*bufferIndex*/)
{
    if (m_buffers.IsEmpty()) return false;

    ID3D12Device* device = dx12Context.Device();
    if (!device) return false;

    TextureBuffer* first = m_buffers[0];
    int w = first->m_info.m_width;
    int h = first->m_info.m_height;
    if (w <= 0 || h <= 0) return false;

    // (Re-)create cubemap resource: 6-slice array
    m_resource.Reset();
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = UINT(w);
    rd.Height           = UINT(h);
    rd.DepthOrArraySize = 6;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_resource));
    if (FAILED(hr)) return false;

    // Upload each face (repeat last buffer if fewer than 6 are provided)
    auto* list = cmdQueue.List();
    if (!list) return false;

    int faceCount = m_buffers.Length();
    for (int face = 0; face < 6; ++face) {
        TextureBuffer* tb = m_buffers[face < faceCount ? face : faceCount - 1];
        const uint8_t* pixels = static_cast<const uint8_t*>(tb->DataBuffer());

        UINT subresource = face; // mip 0, array slice = face

        UINT64 uploadSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        UINT rowCount = 0; UINT64 rowSize = 0;
        device->GetCopyableFootprints(&rd, subresource, 1, 0, &layout, &rowCount, &rowSize, &uploadSize);

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

        uint8_t* mapped = nullptr;
        D3D12_RANGE mapRange{ 0, 0 };
        if (FAILED(upload->Map(0, &mapRange, (void**)&mapped))) return false;
        for (UINT r = 0; r < rowCount; ++r)
            std::memcpy(mapped + layout.Offset + r * layout.Footprint.RowPitch,
                        pixels + r * UINT(w) * 4,
                        UINT(w) * 4);
        upload->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{}, dstLoc{};
        srcLoc.pResource       = upload.Get();
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = layout;
        dstLoc.pResource       = m_resource.Get();
        dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = subresource;
        list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }

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
    srvDesc.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MipLevels         = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = descriptorHeaps.m_srvHeap.CpuHandle(m_handle);
    device->CreateShaderResourceView(m_resource.Get(), &srvDesc, cpuHandle);

    m_isValid   = true;
    m_hasBuffer = true;
    return true;
}

// =================================================================================================
