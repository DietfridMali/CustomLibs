#include "dx12context.h"
#include "resource_view.h"

// =================================================================================================

static uint32_t rtvCount = 0;

bool RTV::Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format) {
    Handle() = descriptorHeaps.AllocRTV();
    if (not IsValid())
        return false;
    D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
    rtvd.Format = format;
    rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dx12Context.Device()->CreateRenderTargetView(resource.Get(), &rtvd, CPUHandle());
    ++rtvCount;
    return true;
}


// One Free for every view type: the handle carries a back-pointer to its owning heap
// (DescriptorHandle::m_heap), so it can release itself regardless of heap type.
void ResourceView::Free(void) noexcept {
    if (m_heap and IsValid())
        m_heap->Free(index);
    Handle() = {};
}


bool RTV::Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format, int arraySlice) {
    Handle() = descriptorHeaps.AllocRTV();
    if (not IsValid())
        return false;
    D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
    rtvd.Format = format;
    // TEXTURE2DARRAY with a slice count of one - that is what addressing a single face of a cube map
    // looks like to a render target. D3D12 has no RTV dimension for cube maps.
    rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtvd.Texture2DArray.MipSlice = 0;
    rtvd.Texture2DArray.FirstArraySlice = UINT(arraySlice);
    rtvd.Texture2DArray.ArraySize = 1;
    rtvd.Texture2DArray.PlaneSlice = 0;
    dx12Context.Device()->CreateRenderTargetView(resource.Get(), &rtvd, CPUHandle());
    ++rtvCount;
    return true;
}


bool SRV::CreateCube(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format)
{
    Handle() = descriptorHeaps.AllocSRV();
    if (not IsValid())
        return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = format;
    srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.TextureCube.MostDetailedMip = 0;
    srvd.TextureCube.MipLevels = 1;
    srvd.TextureCube.ResourceMinLODClamp = 0.0f;
    dx12Context.Device()->CreateShaderResourceView(resource.Get(), &srvd, CPUHandle());
    return true;
}


bool SRV::CreateArray(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format, int layerCount)
{
    Handle() = descriptorHeaps.AllocSRV();
    if (not IsValid())
        return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = format;
    srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2DArray.MostDetailedMip = 0;
    srvd.Texture2DArray.MipLevels = 1;
    srvd.Texture2DArray.FirstArraySlice = 0;
    srvd.Texture2DArray.ArraySize = UINT(layerCount);
    srvd.Texture2DArray.PlaneSlice = 0;
    srvd.Texture2DArray.ResourceMinLODClamp = 0.0f;
    dx12Context.Device()->CreateShaderResourceView(resource.Get(), &srvd, CPUHandle());
    return true;
}


bool SRV::Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format)
{
    Handle() = descriptorHeaps.AllocSRV();
    if (not IsValid())
        return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = format;
    srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels = 1;
    dx12Context.Device()->CreateShaderResourceView(resource.Get(), &srvd, CPUHandle());
    return true;
}


bool DSV::Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format)
{
    Handle()  = descriptorHeaps.AllocDSV();
    if (not IsValid())
        return false;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format = format;
    dsvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dx12Context.Device()->CreateDepthStencilView(resource.Get(), &dsvd, CPUHandle());
    return true;
}


bool DSV::Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format, D3D12_DSV_FLAGS flags)
{
    Handle()  = descriptorHeaps.AllocDSV();
    if (not IsValid())
        return false;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format = format;
    dsvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvd.Flags = flags;
    dx12Context.Device()->CreateDepthStencilView(resource.Get(), &dsvd, CPUHandle());
    return true;
}


// UAV uses the CBV_SRV_UAV heap (descriptorHeaps.AllocSRV) - the heap type is shared.
bool UAV::Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format)
{
    Handle() = descriptorHeaps.AllocSRV();
    if (not IsValid())
        return false;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd{};
    uavd.Format = format;
    uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavd.Texture2D.MipSlice = 0;
    uavd.Texture2D.PlaneSlice = 0;
    dx12Context.Device()->CreateUnorderedAccessView(resource.Get(), nullptr, &uavd, CPUHandle());
    return true;
}

// =================================================================================================
