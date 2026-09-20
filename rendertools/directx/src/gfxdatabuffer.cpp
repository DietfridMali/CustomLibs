#define NOMINMAX

#include "gfxdatabuffer.h"
#include "commandlist.h"
#include "dx12context.h"
#include "resource_handler.h"
#include <cstdio>
#include <cstring>

// =================================================================================================
// DX12 GfxDataBuffer implementation
//
// Data is kept on an upload-heap committed resource (CPU-writable, GPU-readable).
// For the DX12 port this is fine for both dynamic and static meshes; a future optimisation
// would copy static buffers into a default-heap resource.

static_assert(GfxDataBuffer::FRAME_COUNT == CommandList::FRAME_COUNT,
              "GfxDataBuffer upload-slot count must match the command-list frame count");


static DXGI_FORMAT IndexFormatFromComponentType(ComponentType componentType) noexcept
{
    return (componentType == ComponentType::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}


GfxDataBuffer::GfxDataBuffer(const char* type, int id, GfxBufferTarget bufferType, bool isDynamic) noexcept
    : m_index(-1)
    , m_id(id)
    , m_type(type)
    , m_bufferType(bufferType)
    , m_data(nullptr)
    , m_size(0)
    , m_itemSize(0)
    , m_itemCount(0)
    , m_componentCount(0)
    , m_componentType(ComponentType::Float)
    , m_isDynamic(isDynamic)
{ }


size_t GfxDataBuffer::ComponentSize(size_t componentType) noexcept
{
    switch (ComponentType(componentType)) {
        case ComponentType::UInt16: 
            return 2;
        case ComponentType::Float:
        case ComponentType::UInt32:
        default:                    
            return 4;
    }
}


GfxDataBuffer& GfxDataBuffer::Copy(GfxDataBuffer const& other)
{
    if (this != &other) {
        ResourceDescriptor::operator=(other);
        m_index = other.m_index;
        m_type = other.m_type;
        m_id = other.m_id;
        m_bufferType = other.m_bufferType;
        m_data = other.m_data;
        for (int i = 0; i < FRAME_COUNT; ++i)
            m_resource[i] = other.m_resource[i];    // shared ref-count via ComPtr
        m_vbv = other.m_vbv;
        m_ibv = other.m_ibv;
        m_size = other.m_size;
        m_itemSize = other.m_itemSize;
        m_itemCount = other.m_itemCount;
        m_componentCount = other.m_componentCount;
        m_componentType = other.m_componentType;
        m_isDynamic = other.m_isDynamic;
        m_lastUpdateFrame = other.m_lastUpdateFrame;
    }
    return *this;
}


GfxDataBuffer& GfxDataBuffer::Move(GfxDataBuffer& other) noexcept
{
    if (this != &other) {
        ResourceDescriptor::operator=(std::move(other));
        m_index = other.m_index;
        m_type = other.m_type;
        m_id = other.m_id;
        m_bufferType = other.m_bufferType;
        m_data = other.m_data;
        for (int i = 0; i < FRAME_COUNT; ++i)
            m_resource[i] = std::move(other.m_resource[i]);
        m_vbv = other.m_vbv;
        m_ibv = other.m_ibv;
        other.m_vbv = {};
        other.m_ibv = {};
        m_size = other.m_size;
        m_itemSize = other.m_itemSize;
        m_itemCount = other.m_itemCount;
        m_componentCount = other.m_componentCount;
        m_componentType = other.m_componentType;
        m_isDynamic = other.m_isDynamic;
        m_lastUpdateFrame = other.m_lastUpdateFrame;
    }
    return *this;
}


bool GfxDataBuffer::Create(int slot, size_t dataSize) {
    m_resource[slot] = gfxResourceHandler.AcquireUpload(dataSize);
    if (not m_resource[slot])
        return false;
#if DBG_DIRECTX
    char name[128];
    snprintf(name, sizeof(name), "GfxDataBuffer[%s/%d] slot %d", m_type, m_id, slot);
    m_resource[slot]->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(strlen(name)), name);
#endif
    return true;
}


bool GfxDataBuffer::Update(const char* type, GfxBufferTarget bufferType, int index, void* data, size_t dataSize, ComponentType componentType, size_t componentCount, bool /*forceUpdate*/) noexcept
{
    if (not data or (dataSize == 0))
        return false;

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    m_type = type;
    m_bufferType = bufferType;
    m_index = index;
    m_componentType = componentType;
    m_componentCount = int(componentCount);

    m_itemSize = ComponentSize(size_t(componentType)) * componentCount;
    m_itemCount = uint32_t(dataSize / ((m_itemSize > 0) ? m_itemSize : 1));
    m_size = uint32_t(dataSize);

    // Dynamic buffers rotate through FRAME_COUNT upload slots so a write never lands on a
    // resource the GPU may still be reading from a previous in-flight frame; static buffers
    // always use slot 0. Each slot is created once and reused — no per-frame allocation.
    //
    // A second Update of the same buffer within one frame (e.g. MapLayout drawing every map's
    // layout during init, with no frame boundary between them) would otherwise overwrite a
    // slot still referenced by an already-recorded draw — so a same-frame re-update takes a
    // fresh resource. The previous one stays alive via gfxResourceHandler's per-frame tracking
    // until the next Cleanup / FlushResources.
    //
    // A static buffer has no slot rotation to protect it, so ANY update of one that already holds
    // data is treated the same way: the frame before may still be in flight with a draw that reads
    // slot 0, and writing the new data into it would hand that draw the next frame's vertices.
    const uint64_t frameNumber = commandListHandler.FrameNumber();
    const int slot = m_isDynamic ? commandListHandler.FrameIndex() : 0;
    const bool sameFrameReupdate = (frameNumber == m_lastUpdateFrame);
    const bool needsFreshBuffer = sameFrameReupdate or not m_isDynamic;
    if (needsFreshBuffer or not m_resource[slot] or (m_resource[slot]->GetDesc().Width < dataSize)) {
        if (m_resource[slot])
            gfxResourceHandler.TrackUpload(m_resource[slot]);
        if (not Create(slot, dataSize))
            return false;
    }
    m_lastUpdateFrame = frameNumber;
    ID3D12Resource* resource = m_resource[slot].Get();

    // Upload data
    void* mapped = nullptr;
    D3D12_RANGE range{ 0, 0 };
    if (FAILED(resource->Map(0, &range, &mapped)))
        return false;
    std::memcpy(mapped, data, dataSize);
    resource->Unmap(0, nullptr);

    D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = resource->GetGPUVirtualAddress();

    if (bufferType == GfxBufferTarget::Index) {
        m_ibv.BufferLocation = gpuAddr;
        m_ibv.SizeInBytes    = UINT(dataSize);
        m_ibv.Format         = IndexFormatFromComponentType(componentType);
    }
    else {
        m_vbv.BufferLocation  = gpuAddr;
        m_vbv.SizeInBytes     = UINT(dataSize);
        m_vbv.StrideInBytes   = UINT(m_itemSize);
    }

    return true;
}


void GfxDataBuffer::Clear(void) noexcept
{
    for (auto& r : m_resource) {
        if (r)
            gfxResourceHandler.TrackUpload(r);
        r.Reset();
    }
    m_vbv = {};
    m_ibv = {};
    m_id  = 0;
    m_isDynamic = true;
}


void GfxDataBuffer::Destroy(void) noexcept
{
    for (auto& r : m_resource) {
        if (r)
            gfxResourceHandler.TrackUpload(r);
        r.Reset();
    }
    m_vbv = {};
    m_ibv = {};
    m_size = 0;
    m_itemCount = 0;
    m_itemSize = 0;
}

// =================================================================================================
