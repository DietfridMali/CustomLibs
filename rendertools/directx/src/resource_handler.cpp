#include "resource_handler.h"
#include "dx12context.h"
#include "commandlist.h"
#include "descriptor_heap.h"

#include <cstring>

// =================================================================================================

void GfxResourceHandler::Init(int frameCount) noexcept {
    if (frameCount < 1)
        frameCount = 1;
    m_frameResources.Resize(frameCount);
    m_frameRTVs.Resize(frameCount);
}


ComPtr<ID3D12Resource> GfxResourceHandler::GetUploadResource(const char* name, size_t dataSize) {
    ID3D12Device* device = dx12Context.Device();
    if (not device or (dataSize == 0))
        return nullptr;

    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = dataSize;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource))))
        return nullptr;
    Track(resource);
    return resource;
}


void GfxResourceHandler::Track(ComPtr<ID3D12Resource> resource) noexcept {
    if (not resource)
        return;
    const int fi = commandListHandler.FrameIndex();
    if ((fi < 0) or (fi >= m_frameResources.Length()))
        return;
    m_frameResources[fi].Push(std::move(resource));
}


void GfxResourceHandler::Track(const DescriptorHandle& rtvHandle) noexcept {
    if (not rtvHandle.IsValid())
        return;
    const int fi = commandListHandler.FrameIndex();
    if ((fi < 0) or (fi >= m_frameRTVs.Length()))
        return;
    m_frameRTVs[fi].Push(rtvHandle);
}


void GfxResourceHandler::Cleanup(int frameIndex, bool waitIdle) noexcept {
#ifdef _DEBUG
    if ((frameIndex < 0) or (frameIndex >= m_frameRTVs.Length()))
        return;
#endif
    if (waitIdle)
        commandListHandler.CmdQueue().WaitIdle();
    RTVArray& rtvs = m_frameRTVs[frameIndex];
    for (int i = rtvs.Length(); i > 0; ) {
        --i;
        descriptorHeaps.FreeRTV(rtvs[i]);
    }
    rtvs.Clear();
    m_frameResources[frameIndex].Clear();
}

// =================================================================================================
