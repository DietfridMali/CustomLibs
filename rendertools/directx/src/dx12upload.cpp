#include "dx12upload.h"
#include "commandlist.h"
#include "gfxrenderer.h"
#include "resource_handler.h"
#include "texture.h"
#include "descriptor_heap.h"
#include "dx12context.h"
#include "gfxpixelformat_dx.h"
#include "texture_mips.h"

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

#if DBG_DIRECTX
    char name[128];
    snprintf(name, sizeof(name), "Texture Subresource[%d,%d]", width, height);
    outUpload = gfxResourceHandler.GetUploadResource(name, size_t(UINT64(layout.Footprint.RowPitch) * rowCount));
#else
    outUpload = gfxResourceHandler.GetUploadResource("", size_t(UINT64(layout.Footprint.RowPitch) * rowCount));
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
    CommandList* cl = static_cast<CommandList*>(baseRenderer.StartOperation("UploadTextureData"));
    if (not cl)
        return false;

    ComPtr<ID3D12Resource> uploads[6];
    if (faceCount > 6)
        faceCount = 6;
    for (int i = 0; i < faceCount; ++i) {
        if (not UploadSubresource(device, cl->GfxList(), dstResource, UINT(i), faces[i], width, height, channels, uploads[i], /*addBarrier=*/false)) {
            baseRenderer.FinishOperation(cl);
            return false;
        }
    }
    SubresourceBarrier(cl->GfxList(), dstResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
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
#if DBG_DIRECTX
    char name[128];
    snprintf(name, sizeof(name), "Texture3D - resource[%d,%d]", w, h);
    resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rowCount = 0;
    UINT64 rowSize = 0;
    device->GetCopyableFootprints(&rd, 0, 1, 0, &layout, &rowCount, &rowSize, &uploadSize);

#if DBG_DIRECTX
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

    CommandList* cl = static_cast<CommandList*>(baseRenderer.StartOperation("Upload3DTextureData"));
    if (not cl)
        return nullptr;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{}, dstLoc{};
    srcLoc.pResource        = upload.Get();
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint  = layout;
    dstLoc.pResource        = resource.Get();
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;
    cl->GfxList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    SubresourceBarrier(cl->GfxList(), resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    baseRenderer.FinishOperation(cl);
#ifdef _DEBUG
    if (not resource)
        return nullptr;
#endif
    return resource;
}

// =================================================================================================
// Platform-neutral upload helpers

static bool EnsureSRVHandle(uint32_t& handleOut) noexcept
{
    if (handleOut == UINT32_MAX) {
        DescriptorHandle hdl = descriptorHeaps.AllocSRV();
        if (not hdl.IsValid())
            return false;
        handleOut = hdl.index;
    }
    return true;
}


static void CreateSRV2D(uint32_t handle, ID3D12Resource* resource, DXGI_FORMAT fmt) noexcept
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc { };
    srvDesc.Format                  = fmt;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;
    dx12Context.Device()->CreateShaderResourceView(resource, &srvDesc,
        descriptorHeaps.m_srvHeap.CpuHandle(handle));
}


static void CreateSRV3D(uint32_t handle, ID3D12Resource* resource, DXGI_FORMAT fmt,
                        uint32_t mipLevels = 1) noexcept
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc { };
    srvDesc.Format                  = fmt;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture3D.MipLevels     = mipLevels;
    dx12Context.Device()->CreateShaderResourceView(resource, &srvDesc,
        descriptorHeaps.m_srvHeap.CpuHandle(handle));
}


bool Upload2DTexture(Texture& tex, int width, int height,
                     GfxPixelFormat fmt, const void* data) noexcept
{
    if ((data == nullptr) or (width <= 0) or (height <= 0))
        return false;

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    const DXGI_FORMAT dxgi = ToDXGIFormat(fmt);
    const uint32_t stride = GfxPixelStride(fmt);
    if ((dxgi == DXGI_FORMAT_UNKNOWN) or (stride == 0))
        return false;

    tex.m_resource.Reset();

    D3D12_HEAP_PROPERTIES hp { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd { };
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = UINT(width);
    rd.Height           = UINT(height);
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = dxgi;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&tex.m_resource))))
        return false;
#if DBG_DIRECTX
    char name[128];
    snprintf(name, sizeof(name), "Texture2D[%s]", (const char*) tex.m_name);
    tex.m_resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif

    const uint8_t* src = reinterpret_cast<const uint8_t*>(data);
    if (not UploadTextureData(device, tex.m_resource.Get(), src, width, height, int(stride)))
        return false;

    if (not EnsureSRVHandle(tex.m_handle))
        return false;
    CreateSRV2D(tex.m_handle, tex.m_resource.Get(), dxgi);

    tex.SetParams(false);
    tex.m_isValid = true;
    tex.m_isDeployed = true;
    return true;
}


bool Upload3DTexture(Texture& tex, int width, int height, int depth,
                     GfxPixelFormat fmt, const void* data,
                     bool generateMips) noexcept
{
    if ((data == nullptr) or (width <= 0) or (height <= 0) or (depth <= 0))
        return false;

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    const DXGI_FORMAT dxgi = ToDXGIFormat(fmt);
    const uint32_t stride = GfxPixelStride(fmt);
    if ((dxgi == DXGI_FORMAT_UNKNOWN) or (stride == 0))
        return false;

    // CPU-side mip chain (functional equivalent of OGL's glGenerateMipmap).
    // Only built when generateMips=true; otherwise the resource has a single mip level (Mip 0).
    // BuildMipChain3D + the multi-level upload loop stay intact for future opt-in callers.
    AutoArray<MipLevel3D> mipChain;
    if (generateMips)
        BuildMipChain3D(data, width, height, depth, fmt, mipChain);
    const uint32_t mipLevels = generateMips ? uint32_t(mipChain.Length()) : 1u;

    tex.m_resource.Reset();

    D3D12_HEAP_PROPERTIES hp { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd { };
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    rd.Width            = UINT(width);
    rd.Height           = UINT(height);
    rd.DepthOrArraySize = UINT16(depth);
    rd.MipLevels        = UINT16(mipLevels);
    rd.Format           = dxgi;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&tex.m_resource))))
        return false;
#if DBG_DIRECTX
    char name[128];
    snprintf(name, sizeof(name), "Texture3D[%s]", (const char*) tex.m_name);
    tex.m_resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif

    CommandList* cl = static_cast<CommandList*>(baseRenderer.StartOperation("Upload3DTextureMips"));
    if (not cl)
        return false;

    // One upload buffer + CopyTextureRegion per mip level.
    AutoArray<ComPtr<ID3D12Resource>> uploads;
    uploads.Resize(mipLevels);

    for (uint32_t lv = 0; lv < mipLevels; ++lv) {
        // When generateMips=false, mipChain is empty — synthesize Mip 0 from the original input.
        const int mipW = generateMips ? mipChain[lv].width  : width;
        const int mipH = generateMips ? mipChain[lv].height : height;
        const int mipD = generateMips ? mipChain[lv].depth  : depth;
        const uint8_t* src = generateMips ? mipChain[lv].data.Data()
                                          : reinterpret_cast<const uint8_t*>(data);

        D3D12_RESOURCE_DESC mipDesc = rd;
        mipDesc.Width            = UINT(mipW);
        mipDesc.Height           = UINT(mipH);
        mipDesc.DepthOrArraySize = UINT16(mipD);
        mipDesc.MipLevels        = 1;

        UINT64 uploadSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout { };
        UINT rowCount = 0;
        UINT64 rowSize = 0;
        device->GetCopyableFootprints(&mipDesc, 0, 1, 0, &layout, &rowCount, &rowSize, &uploadSize);

        ComPtr<ID3D12Resource> upload = gfxResourceHandler.GetUploadResource("", size_t(uploadSize));
        if (not upload) {
            baseRenderer.FinishOperation(cl);
            return false;
        }

        uint8_t* mapped = nullptr;
        D3D12_RANGE mapRange { 0, 0 };
        if (FAILED(upload->Map(0, &mapRange, (void**) &mapped))) {
            baseRenderer.FinishOperation(cl);
            return false;
        }
        UINT srcRowBytes = UINT(mipW) * stride;
        UINT slicePitch  = layout.Footprint.RowPitch * rowCount;
        if (srcRowBytes == layout.Footprint.RowPitch)
            std::memcpy(mapped + layout.Offset, src, size_t(slicePitch) * UINT(mipD));
        else
            for (UINT z = 0; z < UINT(mipD); ++z)
                for (UINT r = 0; r < rowCount; ++r)
                    std::memcpy(mapped + layout.Offset + z * slicePitch + r * layout.Footprint.RowPitch,
                                src + (z * rowCount + r) * srcRowBytes,
                                srcRowBytes);
        upload->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION srcLoc { }, dstLoc { };
        srcLoc.pResource        = upload.Get();
        srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint  = layout;
        dstLoc.pResource        = tex.m_resource.Get();
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = lv;
        cl->GfxList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        uploads[lv] = upload;
    }

    // Transition all subresources COPY_DEST -> PIXEL_SHADER_RESOURCE.
    SubresourceBarrier(cl->GfxList(), tex.m_resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    baseRenderer.FinishOperation(cl);

    if (not EnsureSRVHandle(tex.m_handle))
        return false;
    CreateSRV3D(tex.m_handle, tex.m_resource.Get(), dxgi, mipLevels);

    tex.SetParams(false);
    tex.m_isValid = true;
    tex.m_isDeployed = true;
    return true;
}

// =================================================================================================
