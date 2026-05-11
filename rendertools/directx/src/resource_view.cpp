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


void RTV::Free(void) {
    descriptorHeaps.FreeRTV(*this);
    *this = {};
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


void SRV::Free(void) {
    descriptorHeaps.FreeSRV(*this);
    *this = {};
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

void DSV::Free(void) {
    descriptorHeaps.FreeDSV(*this);
    *this = {};
}

// =================================================================================================
