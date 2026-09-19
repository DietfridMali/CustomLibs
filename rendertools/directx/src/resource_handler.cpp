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
    m_frameDescriptors.Resize(frameCount);
    m_frameResourceSerials.Resize(frameCount);
    m_frameDescriptorSerials.Resize(frameCount);
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
#if DBG_DIRECTX
    if (name and name[0])
        resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif
    Track(resource);
    return resource;
}


void GfxResourceHandler::Track(ComPtr<ID3D12Resource> resource) noexcept {
    if (s_shutdown or not resource)
        return;
    const int fi = commandListHandler.FrameIndex();
    if ((fi < 0) or (fi >= m_frameResources.Length()))
        return;
    m_frameResources[fi].Push(std::move(resource));
    m_frameResourceSerials[fi].Push(NextSerial());
}


void GfxResourceHandler::Track(const DescriptorHandle& handle) noexcept {
    if (s_shutdown or not handle.IsValid())
        return;
    const int fi = commandListHandler.FrameIndex();
    m_frameDescriptors[fi].Push(handle);
    m_frameDescriptorSerials[fi].Push(NextSerial());
}


void GfxResourceHandler::Cleanup(int frameIndex, bool waitIdle) noexcept {
    // Teardown started — the descriptor heaps may already be gone, so a per-slot Free() would
    // dereference a dangling heap. Nothing to drain anyway (Track() is inert past this point).
    if (s_shutdown)
        return;
    if (waitIdle)
        commandListHandler.CmdQueue().WaitIdle();
    DescriptorArray& descriptors = m_frameDescriptors[frameIndex];
    for (auto& h : descriptors)
        h.m_heap->Free(h.index);   // each handle frees itself into its own heap (clears m_owners too)
    descriptors.Clear();
    m_frameResources[frameIndex].Clear();
    m_frameDescriptorSerials[frameIndex].Clear();
    m_frameResourceSerials[frameIndex].Clear();
}


void GfxResourceHandler::CleanupBefore(int frameIndex, uint64_t serialLimit) noexcept {
    if (s_shutdown)
        return;

    DescriptorArray descriptors = std::move(m_frameDescriptors[frameIndex]);
    SerialArray descriptorSerials = std::move(m_frameDescriptorSerials[frameIndex]);

    m_frameDescriptors[frameIndex].Clear();
    m_frameDescriptorSerials[frameIndex].Clear();
    for (int32_t i = 0; i < descriptors.Length(); ++i) {
        if (descriptorSerials[i] < serialLimit)
            descriptors[i].m_heap->Free(descriptors[i].index);
        else {
            m_frameDescriptors[frameIndex].Push(descriptors[i]);
            m_frameDescriptorSerials[frameIndex].Push(descriptorSerials[i]);
        }
    }

    ResourceArray resources = std::move(m_frameResources[frameIndex]);
    SerialArray resourceSerials = std::move(m_frameResourceSerials[frameIndex]);

    m_frameResources[frameIndex].Clear();
    m_frameResourceSerials[frameIndex].Clear();
    for (int32_t i = 0; i < resources.Length(); ++i) {
        if (resourceSerials[i] < serialLimit)
            continue;
        m_frameResources[frameIndex].Push(std::move(resources[i]));
        m_frameResourceSerials[frameIndex].Push(resourceSerials[i]);
    }
}


void GfxResourceHandler::CleanupAll(void) noexcept {
    for (int i = 0; i < m_frameDescriptors.Length(); ++i)
        Cleanup(i, false);
}

// =================================================================================================
