#include "dx12upload.h"

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


bool UploadSubresource(ID3D12Device* device,
                       ID3D12GraphicsCommandList* list,
                       ID3D12Resource* dstResource,
                       UINT subresource,
                       const uint8_t* pixels,
                       int width, int height, int channels,
                       ComPtr<ID3D12Resource>& outUpload,
                       bool addBarrier) noexcept
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

    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC upDesc{};
    upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upDesc.Width = uploadSize;
    upDesc.Height = upDesc.DepthOrArraySize = upDesc.MipLevels = 1;
    upDesc.SampleDesc.Count = 1;
    upDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &upDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outUpload));
    if (FAILED(hr))
        return false;

    uint8_t* mapped = nullptr;
    D3D12_RANGE mapRange{ 0, 0 };
    if (FAILED(outUpload->Map(0, &mapRange, (void**)&mapped)))
        return false;

    for (UINT r = 0; r < rowCount; ++r) {
        const uint8_t* src = pixels + r * UINT(width) * channels;
        uint8_t* dst = mapped + layout.Offset + r * layout.Footprint.RowPitch;
        if (channels == 4) {
            std::memcpy(dst, src, UINT(width) * 4);
        } else if (channels == 3) {
            for (int x = 0; x < width; ++x) {
                dst[x * 4 + 0] = src[x * 3 + 0];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        } else {
            std::memcpy(dst, src, rowSize);
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
