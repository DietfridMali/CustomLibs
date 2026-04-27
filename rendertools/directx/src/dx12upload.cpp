#include "dx12upload.h"
#include "commandlist.h"
#include "base_renderer.h"
#include "resource_handler.h"

#include <cstring>

// =================================================================================================

void SubresourceBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, UINT subresource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = subresource;
    list->ResourceBarrier(1, &barrier);
}


bool UploadSubresource(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* dstResource, UINT subresource, 
                       const uint8_t* srcBuffer, int width, int height, int channels, ComPtr<ID3D12Resource>& outUpload, bool addBarrier) noexcept
{
    D3D12_RESOURCE_DESC dstDesc = dstResource->GetDesc();

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rowCount = 0;
    UINT64 rowSize = 0;
    device->GetCopyableFootprints(&dstDesc, subresource, 1, 0, &layout, &rowCount, &rowSize, &uploadSize);
    // Each call gets its own upload buffer starting at byte 0.
    // GetCopyableFootprints returns layout.Offset relative to a hypothetical combined buffer;
    // reset it so the map/copy and the placed-footprint source location are both at offset 0.
    layout.Offset = 0;

#ifdef _DEBUG
    char name[128];
    snprintf(name, sizeof(name), "Texture Subresource[%d,%d]", width, height);
    outUpload = gfxResourceHandler.GetUploadResource("", size_t(UINT64(layout.Footprint.RowPitch) * rowCount));
#else
    outUpload = gfxResourceHandler.GetUploadResource(name, size_t(UINT64(layout.Footprint.RowPitch) * rowCount));
#endif
    if (not outUpload)
        return false;

    uint8_t* destBuffer = nullptr;
    D3D12_RANGE mapRange{ 0, 0 };
    if (FAILED(outUpload->Map(0, &mapRange, (void**)&destBuffer)))
        return false;

    UINT srcRowBytes = UINT(width) * UINT(channels);
    if (layout.Footprint.RowPitch == srcRowBytes) {
        std::memcpy(destBuffer + layout.Offset, srcBuffer, rowCount * srcRowBytes);
    }
    else {
        destBuffer += layout.Offset;
        for (UINT r = 0; r < rowCount; ++r) {
            std::memcpy(destBuffer, srcBuffer, srcRowBytes);
            destBuffer += layout.Footprint.RowPitch;
            srcBuffer += srcRowBytes;
        }
    }
    outUpload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION srcLoc{}, dstLoc{};
    srcLoc.pResource = outUpload.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = layout;
    dstLoc.pResource = dstResource;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = subresource;

    list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    if (addBarrier)
        SubresourceBarrier(list, dstResource, subresource);

    return true;
}

// =================================================================================================

bool UploadTextureData(ID3D12Device* device, ID3D12Resource* dstResource, const uint8_t* const* faces, int faceCount, int width, int height, int channels) noexcept
{
    CommandList* cl = baseRenderer.StartOperation("UploadTextureData");
    if (not cl)
        return false;

    ComPtr<ID3D12Resource> uploads[6];
    if (faceCount > 6)
        faceCount = 6;
    for (int i = 0; i < faceCount; ++i) {
        if (not UploadSubresource(device, cl->List(), dstResource, UINT(i), faces[i], width, height, channels, uploads[i], /*addBarrier=*/false)) {
            baseRenderer.FinishOperation(cl);
            return false;
        }
    }
    SubresourceBarrier(cl->List(), dstResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    return baseRenderer.FinishOperation(cl, true);
}


ComPtr<ID3D12Resource> Upload3DTextureData(ID3D12Device* device, int w, int h, int d, DXGI_FORMAT fmt, uint32_t pixelStride, const void* data) noexcept
{
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    rd.Width            = UINT(w);
    rd.Height           = UINT(h);
    rd.DepthOrArraySize = UINT16(d);
    rd.MipLevels        = 1;
    rd.Format           = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource))))
        return nullptr;
#ifdef _DEBUG
    char name[128];
    snprintf(name, sizeof(name), "Texture3D - resource[%d,%d]", w, h);
    resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rowCount = 0;
    UINT64 rowSize = 0;
    device->GetCopyableFootprints(&rd, 0, 1, 0, &layout, &rowCount, &rowSize, &uploadSize);

#ifdef _DEBUG
    snprintf(name, sizeof(name), "Texture3D - upload[%d,%d]", w, h);
    ComPtr<ID3D12Resource> upload = gfxResourceHandler.GetUploadResource(name, size_t(uploadSize));
#else
    ComPtr<ID3D12Resource> upload = gfxResourceHandler.GetUploadResource("", size_t(uploadSize));
#endif
    if (not upload)
        return nullptr;

    const uint8_t* src = static_cast<const uint8_t*>(data);
    uint8_t* mapped = nullptr;
    D3D12_RANGE mapRange{ 0, 0 };
    if (FAILED(upload->Map(0, &mapRange, (void**)&mapped)))
        return nullptr;
    UINT srcRowBytes = UINT(w) * pixelStride;
    UINT slicePitch  = layout.Footprint.RowPitch * rowCount;
    if (srcRowBytes == layout.Footprint.RowPitch)
        std::memcpy(mapped + layout.Offset, src, size_t(slicePitch) * UINT(d));
    else
        for (UINT z = 0; z < UINT(d); ++z)
            for (UINT r = 0; r < rowCount; ++r)
                std::memcpy(mapped + layout.Offset + z * slicePitch + r * layout.Footprint.RowPitch,
                            src + (z * rowCount + r) * srcRowBytes,
                            srcRowBytes);
    upload->Unmap(0, nullptr);

    CommandList* cl = baseRenderer.StartOperation("Upload3DTextureData");
    if (not cl)
        return nullptr;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{}, dstLoc{};
    srcLoc.pResource        = upload.Get();
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint  = layout;
    dstLoc.pResource        = resource.Get();
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;
    cl->List()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    SubresourceBarrier(cl->List(), resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    baseRenderer.FinishOperation(cl);
#ifdef _DEBUG
    if (not resource)
        return nullptr;
#endif
    return resource;
}

// =================================================================================================
