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
        m_resource = other.m_resource;    // shared ref-count via ComPtr
        m_vbv = other.m_vbv;
        m_ibv = other.m_ibv;
        m_size = other.m_size;
        m_itemSize = other.m_itemSize;
        m_itemCount = other.m_itemCount;
        m_componentCount = other.m_componentCount;
        m_componentType = other.m_componentType;
        m_isDynamic = other.m_isDynamic;
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
        m_resource = std::move(other.m_resource);
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
    }
    return *this;
}


bool GfxDataBuffer::Create(size_t dataSize) {
#ifdef _DEBUG
    char name[128];
    snprintf(name, sizeof(name), "GfxDataBuffer[%s/%d] static", m_type, m_id); 
    m_resource = gfxResourceHandler.GetUploadResource(name, dataSize);
#else
    m_resource = gfxResourceHandler.GetUploadResource("", dataSize);
#endif
    return m_resource != nullptr;
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

    if (m_isDynamic or not m_resource or (m_resource->GetDesc().Width < dataSize)) {
        if (not Create(dataSize))
            return false;
    }

    // Upload data
    void* mapped = nullptr;
    D3D12_RANGE range{ 0, 0 };
    if (FAILED(m_resource->Map(0, &range, &mapped)))
        return false;
    std::memcpy(mapped, data, dataSize);
    m_resource->Unmap(0, nullptr);

    D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = m_resource->GetGPUVirtualAddress();

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


void GfxDataBuffer::Destroy(void) noexcept
{
    if (m_resource)
        m_resource.Reset();
    m_vbv = {};
    m_ibv = {};
    m_size = 0;
    m_itemCount = 0;
    m_itemSize = 0;
}

// =================================================================================================
