#define NOMINMAX

#include "vbo.h"
#include "commandlist.h"
#include "dx12context.h"

// =================================================================================================
// DX12 VBO implementation
//
// Data is kept on an upload-heap committed resource (CPU-writable, GPU-readable).
// For the DX12 port this is fine for both dynamic and static meshes; a future optimisation
// would copy static buffers into a default-heap resource.

static DXGI_FORMAT IndexFormatFromComponentType(ComponentType componentType) noexcept
{
    return (componentType == ComponentType::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}


VBO::VBO(const char* type, int id, GfxBufferTarget bufferType, bool isDynamic) noexcept
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


size_t VBO::ComponentSize(size_t componentType) noexcept
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


VBO& VBO::Copy(VBO const& other)
{
    if (this != &other) {
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


VBO& VBO::Move(VBO& other) noexcept
{
    if (this != &other) {
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


bool VBO::Create(ID3D12Device* device, size_t dataSize) {
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = dataSize;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_resource));
    return not FAILED(hr);
}


bool VBO::Update(const char* type, GfxBufferTarget bufferType, int index, void* data, size_t dataSize, ComponentType componentType, size_t componentCount, bool /*forceUpdate*/) noexcept
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

    size_t compSz = ComponentSize(size_t(componentType));
    m_itemSize = compSz * componentCount;
    m_itemCount = uint32_t(dataSize / ((m_itemSize > 0) ? m_itemSize : 1));
    m_size = uint32_t(dataSize);

    CommandList* cl = commandListHandler.GetCurrentCmdListObj();
    if (m_isDynamic and cl) {
        // Multi-buffer path: each Update within one recording session gets its own chunk.
        // The chunk pool is per frame slot (indexed by FrameIndex()), so reuse is safe
        // because BeginFrame waits for the fence covering the previous use of this slot.
        UINT fi = commandListHandler.CmdQueue().FrameIndex();
        if (cl->m_id != m_lastCmdListId[fi] or cl->m_executionId != m_lastExecutionId[fi]) {
            m_chunkIndex[fi] = 0;
            m_lastCmdListId[fi]    = cl->m_id;
            m_lastExecutionId[fi]  = cl->m_executionId;
        }

        int ci = m_chunkIndex[fi];
        if (ci >= int(m_chunks[fi].size())) {
            // grow pool: allocate new chunk
            m_chunks[fi].emplace_back();
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width            = dataSize;
            rd.Height           = rd.DepthOrArraySize = rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_chunks[fi].back()));
            if (FAILED(hr)) {
                m_chunks[fi].pop_back();
                return false;
            }
        } else if (m_chunks[fi][ci]->GetDesc().Width < dataSize) {
            // existing chunk too small — reallocate in place
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width            = dataSize;
            rd.Height           = rd.DepthOrArraySize = rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            m_chunks[fi][ci].Reset();
            HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_chunks[fi][ci]));
            if (FAILED(hr))
                return false;
        }

        m_resource = m_chunks[fi][ci];
        ++m_chunkIndex[fi];
    } else {
        // Static / no active command list: (re-)create single buffer if size changed
        if (not m_resource or (m_resource->GetDesc().Width < dataSize)) {
            if (not Create(device, dataSize))
                return false;
        }
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


void VBO::Destroy(void) noexcept
{
    for (UINT i = 0; i < FRAME_COUNT; ++i)
        m_chunks[i].clear();
    m_resource.Reset();
    m_vbv = {};
    m_ibv = {};
    m_size = 0;
    m_itemCount = 0;
    m_itemSize = 0;
}

// =================================================================================================
