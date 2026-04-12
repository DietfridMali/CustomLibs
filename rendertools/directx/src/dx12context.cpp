#include "dx12context.h"

#include <cstdio>

// =================================================================================================

bool DX12Context::SelectAdapter(void) noexcept {
    UINT adapterIndex = 0;
    ComPtr<IDXGIAdapter1> adapter;
    SIZE_T maxVRAM = 0;

    while (m_factory->EnumAdapters1(adapterIndex++, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
            continue;
        if (desc.DedicatedVideoMemory > maxVRAM) {
            maxVRAM = desc.DedicatedVideoMemory;
            m_adapter = adapter;
        }
    }
    return m_adapter != nullptr;
}


bool DX12Context::Create(bool enableDebugLayer) noexcept {
#ifdef _DEBUG
    if (enableDebugLayer) {
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugController))))
            m_debugController->EnableDebugLayer();
    }
#endif

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) {
        fprintf(stderr, "DX12Context: Failed to create DXGI factory (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    if (!SelectAdapter()) {
        fprintf(stderr, "DX12Context: No suitable DX12 adapter found\n");
        return false;
    }

    hr = D3D12CreateDevice(m_adapter.Get(), m_featureLevel, IID_PPV_ARGS(&m_device));
    if (FAILED(hr)) {
        fprintf(stderr, "DX12Context: Failed to create D3D12 device (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

#ifdef _DEBUG
    if (enableDebugLayer) {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
    }
#endif

    return true;
}

// =================================================================================================
