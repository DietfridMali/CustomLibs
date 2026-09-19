#define NOMINMAX

#include "readtarget.h"
#include "rendertarget.h"
#include "commandlist.h"
#include "dx12context.h"
#include "resource_handler.h"

#include <cstring>

// =================================================================================================

bool GfxReadTarget::Request(RenderTarget& renderTarget, int bufferIndex, int arraySlice) {
    if (not IsIdle())
        return false;
    return renderTarget.ReadBufferAsync(bufferIndex, *this, arraySlice);
}


bool GfxReadTarget::Allocate(UINT64 totalSize) {
    if (totalSize == 0)
        return false;
    if (m_resource and (m_capacity >= size_t(totalSize)))
        return true;

    ID3D12Device* device = dx12Context.Device();

    if (not device)
        return false;
    if (m_resource) {
        m_resource->Unmap(0, nullptr);
        m_mapped = nullptr;
        gfxResourceHandler.Track(m_resource);
        m_resource.Reset();
        m_capacity = 0;
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    D3D12_RESOURCE_DESC desc{};

    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = totalSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_resource))))
        return false;
#if DBG_DIRECTX
    {
        static const char name[] = "GfxReadTarget readback";
        m_resource->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(sizeof(name) - 1), name);
    }
#endif

    void* mapped = nullptr;

    if (FAILED(m_resource->Map(0, nullptr, &mapped))) {
        m_resource.Reset();
        return false;
    }
    m_mapped = static_cast<uint8_t*>(mapped);
    m_capacity = size_t(totalSize);
    return true;
}


bool GfxReadTarget::Submit(const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout, UINT rowCount, UINT64 rowSize, int width, int height, uint64_t frame, int slot) {
    size_t size = size_t(rowSize) * size_t(rowCount);

    if (size > size_t(INT32_MAX))
        return false;
    try {
        m_pixels.Resize(int32_t(size));
    }
    catch (...) {
        return false;
    }
    if (size_t(m_pixels.Length()) < size)
        return false;
    m_layout = layout;
    m_rowCount = rowCount;
    m_rowSize = rowSize;
    SetRequest(size, width, height, frame, slot);
    return true;
}


bool GfxReadTarget::IsComplete(void) noexcept {
    uint64_t frame = commandListHandler.FrameNumber();

    if (frame <= m_requestFrame)
        return false;
    if (frame >= m_requestFrame + uint64_t(CommandQueue::FRAME_COUNT))
        return true;

    CommandQueue& queue = commandListHandler.CmdQueue();

    if (not queue.m_fence)
        return false;
    return queue.m_fence->GetCompletedValue() >= queue.m_fenceValues[m_requestSlot];
}


BaseReadTarget::State GfxReadTarget::Poll(void) {
    if (m_state != State::Pending)
        return m_state;
    if (not IsComplete())
        return m_state;
    if (not m_mapped) {
        m_state = State::Idle;
        return m_state;
    }

    uint8_t* dest = m_pixels.Data();

    for (UINT row = 0; row < m_rowCount; ++row)
        memcpy(dest + size_t(row) * size_t(m_rowSize),
               m_mapped + size_t(m_layout.Offset) + size_t(row) * size_t(m_layout.Footprint.RowPitch),
               size_t(m_rowSize));
    m_state = State::Ready;
    return m_state;
}


const uint8_t* GfxReadTarget::Data(void) {
    return (m_state == State::Ready) ? m_pixels.Data() : nullptr;
}


void GfxReadTarget::Release(void) {
    m_state = State::Idle;
}


void GfxReadTarget::Destroy(void) {
    Release();
    if (m_resource) {
        m_resource->Unmap(0, nullptr);
        gfxResourceHandler.Track(m_resource);
        m_resource.Reset();
    }
    m_mapped = nullptr;
    m_pixels.Reset();
    m_capacity = 0;
    m_size = 0;
}
