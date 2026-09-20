#include "resource_handler.h"
#include "dx12context.h"
#include "commandlist.h"
#include "descriptor_heap.h"
#include "tracy_wrapper.h"

#include <cstdio>
#include <cstring>

// =================================================================================================

void GfxResourceHandler::Init(int frameCount) noexcept {
    if (frameCount < 1)
        frameCount = 1;
    m_frameResources.Resize(frameCount);
    m_frameUploads.Resize(frameCount);
    m_frameDescriptors.Resize(frameCount);
    m_frameResourceSerials.Resize(frameCount);
    m_frameUploadSerials.Resize(frameCount);
    m_frameDescriptorSerials.Resize(frameCount);
}


int GfxResourceHandler::UploadBucket(size_t size) noexcept {
    int bucket = kSmallestUploadBucket;

    while ((bucket < kUploadBuckets - 1) and ((size_t(1) << bucket) < size))
        ++bucket;
    return bucket;
}


ComPtr<ID3D12Resource> GfxResourceHandler::AcquireUpload(size_t size) noexcept {
    ID3D12Device* device = dx12Context.Device();
    if (not device or (size == 0))
        return nullptr;

    const int bucket = UploadBucket(size);
    const size_t bucketSize = size_t(1) << bucket;

    if (bucketSize < size)
        return nullptr;
    if (m_uploadPool[bucket].Length() > 0) {
        ComPtr<ID3D12Resource> resource = m_uploadPool[bucket].Pop();
        m_uploadPoolBytes -= bucketSize;
        ++m_uploadsReused;
        return resource;
    }

    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bucketSize;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource))))
        return nullptr;
    ++m_uploadsCreated;
    return resource;
}


bool GfxResourceHandler::Recycle(ComPtr<ID3D12Pageable>& resource) noexcept {
    if (s_shutdown or not resource)
        return false;

    ComPtr<ID3D12Resource> buffer;

    if (FAILED(resource.As(&buffer)) or not buffer)
        return false;

    D3D12_HEAP_PROPERTIES heapProperties{};
    D3D12_HEAP_FLAGS heapFlags{};

    if (FAILED(buffer->GetHeapProperties(&heapProperties, &heapFlags)) or (heapProperties.Type != D3D12_HEAP_TYPE_UPLOAD))
        return false;

    D3D12_RESOURCE_DESC desc = buffer->GetDesc();

    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
        return false;

    const int bucket = UploadBucket(size_t(desc.Width));

    if ((size_t(1) << bucket) != size_t(desc.Width))
        return false;
    if (m_uploadPoolBytes + size_t(desc.Width) > kUploadPoolLimit)
        return false;
    m_uploadPool[bucket].Push(buffer);
    m_uploadPoolBytes += size_t(desc.Width);
    return true;
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


void GfxResourceHandler::Track(ComPtr<ID3D12Pageable> resource) noexcept {
    if (s_shutdown or not resource)
        return;
    const int fi = commandListHandler.FrameIndex();
    if ((fi < 0) or (fi >= m_frameResources.Length()))
        return;
    m_frameResources[fi].Push(std::move(resource));
    m_frameResourceSerials[fi].Push(NextSerial());
}


void GfxResourceHandler::TrackUpload(ComPtr<ID3D12Resource> resource) noexcept {
    if (s_shutdown or not resource)
        return;
    const int fi = commandListHandler.FrameIndex();
    if ((fi < 0) or (fi >= m_frameUploads.Length()))
        return;
    m_frameUploads[fi].Push(std::move(resource));
    m_frameUploadSerials[fi].Push(NextSerial());
}


void GfxResourceHandler::Track(const DescriptorHandle& handle) noexcept {
    if (s_shutdown or not handle.IsValid())
        return;
    const int fi = commandListHandler.FrameIndex();
    if ((fi < 0) or (fi >= m_frameDescriptors.Length()))
        return;
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
    m_frameUploads[frameIndex].Clear();
    m_frameDescriptorSerials[frameIndex].Clear();
    m_frameResourceSerials[frameIndex].Clear();
    m_frameUploadSerials[frameIndex].Clear();
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
        if (resourceSerials[i] < serialLimit) {
            ++m_resourcesReleased;
            continue;
        }
        m_frameResources[frameIndex].Push(std::move(resources[i]));
        m_frameResourceSerials[frameIndex].Push(resourceSerials[i]);
    }

    ResourceArray uploads = std::move(m_frameUploads[frameIndex]);
    SerialArray uploadSerials = std::move(m_frameUploadSerials[frameIndex]);

    m_frameUploads[frameIndex].Clear();
    m_frameUploadSerials[frameIndex].Clear();
    for (int32_t i = 0; i < uploads.Length(); ++i) {
        if (uploadSerials[i] < serialLimit) {
            if (not Recycle(uploads[i]))
                ++m_resourcesReleased;
            continue;
        }
        m_frameUploads[frameIndex].Push(std::move(uploads[i]));
        m_frameUploadSerials[frameIndex].Push(uploadSerials[i]);
    }
    TracyPlot("DX uploads created", int64_t(m_uploadsCreated));
    TracyPlot("DX uploads reused", int64_t(m_uploadsReused));
    TracyPlot("DX resources released", int64_t(m_resourcesReleased));
    TracyPlot("DX upload pool MB", int64_t(m_uploadPoolBytes >> 20));
    m_uploadsCreated = 0;
    m_uploadsReused = 0;
    m_resourcesReleased = 0;
}


void GfxResourceHandler::CleanupAll(void) noexcept {
    for (int i = 0; i < m_frameDescriptors.Length(); ++i)
        Cleanup(i, false);
    ReleaseUploadPool();
}


void GfxResourceHandler::ReleaseUploadPool(void) noexcept {
    for (auto& bucket : m_uploadPool)
        bucket.Reset();
    m_uploadPoolBytes = 0;
}

// =================================================================================================
