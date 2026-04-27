#include "dx12context.h"
#include "commandlist.h"

#include <cstdio>
#include <memory>

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

        // DRED: auto-breadcrumbs track which GPU op was in flight when the device hung.
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
            dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }
#endif

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) {
        fprintf(stderr, "DX12Context: Failed to create DXGI factory (hr=0x%08X)\n", (unsigned)hr);
        return false;
    }

    if (not SelectAdapter()) {
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
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&m_infoQueue)))) {
            // All severity breaks disabled — DrainMessages() after each Execute prints everything.
            m_infoQueue->SetBreakOnID(D3D12_MESSAGE_ID(209), TRUE);  // vertex buffer stride < input layout stride
            m_infoQueue->SetBreakOnID(D3D12_MESSAGE_ID(210), TRUE);  // vertex buffer too small for draw call
        }
    }
#endif

    return true;
}

// =================================================================================================

#ifdef _DEBUG
void DX12Context::DumpDRED(void) noexcept {
    if (not m_device)
        return;
    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&dred))))
        return;

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc))) {
        const D3D12_AUTO_BREADCRUMB_NODE* node = bc.pHeadAutoBreadcrumbNode;
        for (int n = 0; node; node = node->pNext, ++n) {
            const char* clName = node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "(unnamed)";
            const char* cqName = node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "(unnamed)";
            UINT last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
            fprintf(stderr, "DRED node %d: CL=%s CQ=%s  ops=%u last=%u\n",
                n, clName, cqName, node->BreadcrumbCount, last);
            UINT from = (last > 3 ? last - 3 : 0);
            UINT to   = std::min(last + 10, node->BreadcrumbCount - 1);
            for (UINT i = from; i <= to; ++i)
                fprintf(stderr, "  [%u] op=%u%s\n", i, (unsigned)node->pCommandHistory[i],
                    i == last ? " <-- last completed" : (i > last ? " <-- pending/hung" : ""));
        }
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT pf{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) && pf.PageFaultVA != 0)
        fprintf(stderr, "DRED page fault VA=0x%llX\n", (unsigned long long)pf.PageFaultVA);

    fflush(stderr);
}


void DX12Context::DrainMessages(void) noexcept {
    if (not m_infoQueue)
        return;
    UINT64 count = m_infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        // In Unicode builds the preprocessor expands GetMessage → GetMessageW
        // when d3d12sdklayers.h is parsed, so the vtable entry is named GetMessageW.
        SIZE_T len = 0;
        m_infoQueue->GetMessageW(i, nullptr, &len);
        if (len == 0)
            continue;
        auto buf = std::make_unique<char[]>(len);
        D3D12_MESSAGE* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.get());
        m_infoQueue->GetMessageW(i, msg, &len);
        const char* sev = "INFO";
        if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) 
            sev = "CORRUPTION";
        else if (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR)   
            sev = "ERROR";
        else if (msg->Severity == D3D12_MESSAGE_SEVERITY_WARNING) 
            sev = "WARNING";
        CommandList* cl = commandListHandler.CurrentCmdList();
        // split to make debugging real errors easier
#if 1
        if ((msg->Severity == D3D12_MESSAGE_SEVERITY_INFO) or (msg->Severity == D3D12_MESSAGE_SEVERITY_WARNING))
            ; // fprintf(stderr, "D3D12 %s (id=%u) [CL: %s]: %s\n", sev, (unsigned)msg->ID, cl ? (const char*)cl->GetName() : "(none)", msg->pDescription);
        else
#endif
3            fprintf(stderr, "D3D12 %s (id=%u) [CL: %s]: %s\n", sev, (unsigned)msg->ID, cl ? (const char*)cl->GetName() : "(none)", msg->pDescription);
    }
    m_infoQueue->ClearStoredMessages();
    fflush(stderr);
}
#endif

// =================================================================================================
