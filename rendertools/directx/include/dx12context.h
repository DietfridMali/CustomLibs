#pragma once

#include "framework.h"
#include "basesingleton.hpp"

// =================================================================================================
// DX12Context: Singleton managing the D3D12 device, DXGI factory and selected adapter.
// Must be created before any other DX12 resource.

class DX12Context : public BaseSingleton<DX12Context>
{
public:
    ComPtr<ID3D12Device>        m_device;
    ComPtr<IDXGIFactory4>       m_factory;
    ComPtr<IDXGIAdapter1>       m_adapter;
    D3D_FEATURE_LEVEL           m_featureLevel{ D3D_FEATURE_LEVEL_12_0 };

#ifdef _DEBUG
    ComPtr<ID3D12Debug>         m_debugController;
#endif

    // Creates the DXGI factory, selects the best adapter (highest VRAM, non-software),
    // and creates the D3D12 device. Returns false on any failure.
    bool Create(bool enableDebugLayer = false) noexcept;

    inline ID3D12Device* Device(void) const noexcept { return m_device.Get(); }

private:
    bool SelectAdapter(void) noexcept;
};

#define dx12Context DX12Context::Instance()

// =================================================================================================
