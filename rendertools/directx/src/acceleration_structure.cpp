#include "dx12framework.h"

#include "acceleration_structure.h"
#include "dx12context.h"
#include "commandlist.h"
#include "resource_handler.h"

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

// =================================================================================================

namespace
{
    uint32_t g_scratchAlignment { D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT };
    bool     g_loaded           { false };

    ComPtr<ID3D12Device5> RayTracingDevice(void) noexcept
    {
        ComPtr<ID3D12Device5> device;
        ID3D12Device* baseDevice = dx12Context.Device();
        if (baseDevice)
            baseDevice->QueryInterface(IID_PPV_ARGS(device.GetAddressOf()));
        return device;
    }

    ComPtr<ID3D12GraphicsCommandList4> RayTracingList(ID3D12GraphicsCommandList* list) noexcept
    {
        ComPtr<ID3D12GraphicsCommandList4> rayTracingList;
        if (list)
            list->QueryInterface(IID_PPV_ARGS(rayTracingList.GetAddressOf()));
        return rayTracingList;
    }

    bool CreateDefaultBuffer(ComPtr<ID3D12Resource>& buffer, UINT64 size, D3D12_RESOURCE_STATES state, [[maybe_unused]] const char* name) noexcept
    {
        ID3D12Device* device = dx12Context.Device();
        if (not device or (size == 0))
            return false;

        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = size;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf()))))
            return false;
#if DBG_DIRECTX
        buffer->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(std::strlen(name)), name);
#endif
        return true;
    }

    void ReleaseBuffer(ComPtr<ID3D12Resource>& buffer) noexcept
    {
        if (buffer and not GfxResourceHandler::IsShuttingDown())
            gfxResourceHandler.Track(buffer);
        buffer.Reset();
    }

    void ReleaseUpload(ComPtr<ID3D12Resource>& buffer) noexcept
    {
        if (buffer and not GfxResourceHandler::IsShuttingDown())
            gfxResourceHandler.TrackUpload(buffer);
        buffer.Reset();
    }
}

// =================================================================================================

bool RayTracingApi::Load(ID3D12Device* device) noexcept
{
    if (g_loaded)
        return true;
    if (not device)
        return false;

    ComPtr<ID3D12Device5> rayTracingDevice;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(rayTracingDevice.GetAddressOf())))) {
        fprintf(stderr, "RayTracingApi::Load: ID3D12Device5 is not available\n");
        return false;
    }

    g_scratchAlignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    g_loaded = true;
    return true;
}


bool RayTracingApi::IsLoaded(void) noexcept
{
    return g_loaded;
}


uint32_t RayTracingApi::ScratchAlignment(void) noexcept
{
    return g_scratchAlignment;
}

// =================================================================================================

namespace
{
    struct PreparedBuild
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>         geoms;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc { };
        UINT64                                             scratchSize   { 0 };
        UINT64                                             scratchOffset { 0 };
        AccelerationStructure*                             target { nullptr };
    };


bool PrepareBottomLevel(AccelerationStructure& as, const AccelGeometryDesc* geometries,
                        uint32_t geometryCount, PreparedBuild& out) noexcept
{
    if ((not geometries) or (geometryCount == 0))
        return false;

    as.Destroy();

    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;
    for (uint32_t i = 0; i < geometryCount; ++i) {
        if ((not geometries[i].vertices) or (not geometries[i].indices))
            return false;
        if ((geometries[i].vertexCount == 0) or (geometries[i].indexCount < 3))
            return false;
        totalVertices += geometries[i].vertexCount;
        totalIndices += geometries[i].indexCount;
    }

    as.m_vertices = gfxResourceHandler.AcquireUpload(size_t(totalVertices) * 3 * sizeof(float));
    if (not as.m_vertices) {
        fprintf(stderr, "AccelerationStructure: vertex buffer allocation failed\n");
        as.Destroy();
        return false;
    }
    as.m_indices = gfxResourceHandler.AcquireUpload(size_t(totalIndices) * sizeof(uint32_t));
    if (not as.m_indices) {
        fprintf(stderr, "AccelerationStructure: index buffer allocation failed\n");
        as.Destroy();
        return false;
    }

    D3D12_RANGE readRange{ 0, 0 };
    void* vertexData = nullptr;
    void* indexData = nullptr;
    if (FAILED(as.m_vertices->Map(0, &readRange, &vertexData)) or not vertexData) {
        fprintf(stderr, "AccelerationStructure: build input buffers could not be mapped\n");
        as.Destroy();
        return false;
    }
    if (FAILED(as.m_indices->Map(0, &readRange, &indexData)) or not indexData) {
        fprintf(stderr, "AccelerationStructure: build input buffers could not be mapped\n");
        as.m_vertices->Unmap(0, nullptr);
        as.Destroy();
        return false;
    }
    float* pVertex = static_cast<float*>(vertexData);
    uint32_t* pIndex = static_cast<uint32_t*>(indexData);

    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geoms = out.geoms;

    geoms.resize(geometryCount);

    const D3D12_GPU_VIRTUAL_ADDRESS vertexBase = as.m_vertices->GetGPUVirtualAddress();
    const D3D12_GPU_VIRTUAL_ADDRESS indexBase = as.m_indices->GetGPUVirtualAddress();

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    for (uint32_t i = 0; i < geometryCount; ++i) {
        const AccelGeometryDesc& src = geometries[i];
        const uint32_t primitiveCount = src.indexCount / 3;

        std::memcpy(pVertex + size_t(vertexOffset) * 3, src.vertices, size_t(src.vertexCount) * 3 * sizeof(float));
        for (uint32_t j = 0; j < src.indexCount; ++j)
            pIndex[indexOffset + j] = src.indices[j] + vertexOffset;

        D3D12_RAYTRACING_GEOMETRY_DESC& geom = geoms[i];
        geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geom.Flags = src.opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        geom.Triangles.Transform3x4 = 0;
        geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geom.Triangles.IndexCount = primitiveCount * 3;
        geom.Triangles.VertexCount = totalVertices;
        geom.Triangles.IndexBuffer = indexBase + D3D12_GPU_VIRTUAL_ADDRESS(indexOffset) * sizeof(uint32_t);
        geom.Triangles.VertexBuffer.StartAddress = vertexBase;
        geom.Triangles.VertexBuffer.StrideInBytes = 3 * sizeof(float);

        as.m_primitives += primitiveCount;
        vertexOffset += src.vertexCount;
        indexOffset += src.indexCount;
    }

    as.m_vertices->Unmap(0, nullptr);
    as.m_indices->Unmap(0, nullptr);

    out.target = &as;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = out.buildDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = geometryCount;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = geoms.data();

    ComPtr<ID3D12Device5> device = RayTracingDevice();
    if (not device) {
        as.Destroy();
        return false;
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes { };
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
    if (sizes.ResultDataMaxSizeInBytes == 0) {
        fprintf(stderr, "AccelerationStructure: build size is zero\n");
        as.Destroy();
        return false;
    }

    if (not CreateDefaultBuffer(as.m_storage[0], sizes.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "AccelerationStructure bottom level")) {
        fprintf(stderr, "AccelerationStructure: storage buffer allocation failed (%llu bytes)\n",
                static_cast<unsigned long long>(sizes.ResultDataMaxSizeInBytes));
        as.Destroy();
        return false;
    }

    out.buildDesc.DestAccelerationStructureData = as.m_storage[0]->GetGPUVirtualAddress();
    as.m_storageSize[0] = sizes.ResultDataMaxSizeInBytes;
    out.scratchSize = sizes.ScratchDataSizeInBytes;
    return true;
}
}

// =================================================================================================

bool AccelerationStructure::BuildBottomLevelBatch(const AccelBuildItem* items, uint32_t itemCount) noexcept
{
    if (not RayTracingApi::IsLoaded())
        return false;
    if ((not items) or (itemCount == 0))
        return false;

    const UINT64 alignment = UINT64(RayTracingApi::ScratchAlignment());

    std::vector<PreparedBuild> prepared(itemCount);
    UINT64 scratchTotal = 0;
    uint32_t nPrepared = 0;

    for (uint32_t i = 0; i < itemCount; ++i) {
        if (not items[i].structure)
            continue;
        PreparedBuild& p = prepared[nPrepared];
        if (not PrepareBottomLevel(*items[i].structure, items[i].geometries, items[i].geometryCount, p))
            continue;
        p.scratchOffset = scratchTotal;
        scratchTotal += (p.scratchSize + alignment - 1) & ~(alignment - 1);
        ++nPrepared;
    }
    if (nPrepared == 0)
        return false;

    ComPtr<ID3D12Resource> scratch;
    if (not CreateDefaultBuffer(scratch, scratchTotal + alignment, D3D12_RESOURCE_STATE_COMMON, "AccelerationStructure scratch")) {
        fprintf(stderr, "AccelerationStructure: scratch allocation failed (%llu bytes)\n",
                static_cast<unsigned long long>(scratchTotal));
        for (uint32_t i = 0; i < nPrepared; ++i)
            prepared[i].target->Destroy();
        return false;
    }
    D3D12_GPU_VIRTUAL_ADDRESS scratchBase = scratch->GetGPUVirtualAddress();
    scratchBase = (scratchBase + alignment - 1) & ~(alignment - 1);

    CommandList* cl = commandListHandler.CreateCmdList("AccelerationStructure::BuildBottomLevelBatch", true);
    if (not (cl and cl->Open())) {
        ReleaseBuffer(scratch);
        for (uint32_t i = 0; i < nPrepared; ++i)
            prepared[i].target->Destroy();
        return false;
    }

    ComPtr<ID3D12GraphicsCommandList4> list = RayTracingList(cl->GfxList());
    if (list) {
        for (uint32_t i = 0; i < nPrepared; ++i) {
            PreparedBuild& p = prepared[i];
            p.buildDesc.Inputs.pGeometryDescs = p.geoms.data();
            p.buildDesc.ScratchAccelerationStructureData = scratchBase + p.scratchOffset;
            list->BuildRaytracingAccelerationStructure(&p.buildDesc, 0, nullptr);
        }
    }
    cl->Flush();
    const bool submitted = (list != nullptr);
    ReleaseBuffer(scratch);

    for (uint32_t i = 0; i < nPrepared; ++i) {
        AccelerationStructure& as = *prepared[i].target;
        if (not submitted) {
            as.Destroy();
            continue;
        }
        as.m_address = as.m_storage[0]->GetGPUVirtualAddress();
        ReleaseUpload(as.m_vertices);
        ReleaseUpload(as.m_indices);
    }
    return submitted;
}

// =================================================================================================

bool AccelerationStructure::BuildBottomLevel(const AccelGeometryDesc* geometries, uint32_t geometryCount) noexcept
{
    AccelBuildItem item;

    item.structure = this;
    item.geometries = geometries;
    item.geometryCount = geometryCount;
    return BuildBottomLevelBatch(&item, 1);
}

// =================================================================================================

bool AccelerationStructure::BuildTopLevel(const AccelInstance* instances, uint32_t instanceCount, bool immediate) noexcept
{
    if (not RayTracingApi::IsLoaded())
        return false;
    if ((not instances) or (instanceCount == 0))
        return false;

    ComPtr<ID3D12Device5> device = RayTracingDevice();
    if (not device)
        return false;

    m_slot = uint32_t(commandListHandler.FrameIndex()) % kFrameSlots;

    ComPtr<ID3D12Resource>& instanceBuffer = m_instances[m_slot];

    if (instanceCount > m_instanceCapacity[m_slot]) {
        ReleaseUpload(instanceBuffer);
        instanceBuffer = gfxResourceHandler.AcquireUpload(size_t(instanceCount) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
        if (not instanceBuffer) {
            fprintf(stderr, "AccelerationStructure::BuildTopLevel: instance buffer allocation failed (%u)\n", instanceCount);
            m_instanceCapacity[m_slot] = 0;
            return false;
        }
        m_instanceCapacity[m_slot] = instanceCount;
    }

    D3D12_RANGE readRange{ 0, 0 };
    void* instanceData = nullptr;
    if (FAILED(instanceBuffer->Map(0, &readRange, &instanceData)) or not instanceData) {
        fprintf(stderr, "AccelerationStructure::BuildTopLevel: instance buffer could not be mapped\n");
        return false;
    }
    D3D12_RAYTRACING_INSTANCE_DESC* pInstance = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(instanceData);

    uint32_t written = 0;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        const AccelInstance& src = instances[i];
        if ((not src.blas) or (not src.blas->IsValid()))
            continue;
        D3D12_RAYTRACING_INSTANCE_DESC dst { };
        std::memcpy(dst.Transform, src.transform, sizeof(float) * 12);
        dst.InstanceID = src.customIndex & 0xFFFFFFu;
        dst.InstanceMask = src.mask & 0xFFu;
        dst.InstanceContributionToHitGroupIndex = 0;
        dst.Flags = src.singleSided ? UINT(D3D12_RAYTRACING_INSTANCE_FLAG_NONE) : UINT(D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE);
        dst.AccelerationStructure = src.blas->DeviceAddress();
        std::memcpy(pInstance + written, &dst, sizeof(dst));
        ++written;
    }
    D3D12_RANGE writtenRange{ 0, SIZE_T(written) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC) };
    instanceBuffer->Unmap(0, &writtenRange);
    if (written == 0)
        return false;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc { };
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = buildDesc.Inputs;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = written;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = instanceBuffer->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes { };
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
    if (sizes.ResultDataMaxSizeInBytes == 0) {
        fprintf(stderr, "AccelerationStructure::BuildTopLevel: build size is zero\n");
        return false;
    }

    if ((not m_storage[m_slot]) or (m_storageSize[m_slot] < sizes.ResultDataMaxSizeInBytes)) {
        ReleaseBuffer(m_storage[m_slot]);
        m_storageSize[m_slot] = 0;
        if (not CreateDefaultBuffer(m_storage[m_slot], sizes.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "AccelerationStructure top level")) {
            fprintf(stderr, "AccelerationStructure::BuildTopLevel: storage allocation failed (%llu bytes)\n",
                    static_cast<unsigned long long>(sizes.ResultDataMaxSizeInBytes));
            return false;
        }
        m_storageSize[m_slot] = sizes.ResultDataMaxSizeInBytes;
    }

    const UINT64 alignment = UINT64(RayTracingApi::ScratchAlignment());
    const UINT64 scratchNeeded = sizes.ScratchDataSizeInBytes + alignment;

    if (m_scratchSize[m_slot] < scratchNeeded) {
        ReleaseBuffer(m_scratch[m_slot]);
        m_scratchSize[m_slot] = 0;
        if (not CreateDefaultBuffer(m_scratch[m_slot], scratchNeeded, D3D12_RESOURCE_STATE_COMMON, "AccelerationStructure scratch")) {
            fprintf(stderr, "AccelerationStructure::BuildTopLevel: scratch allocation failed (%llu bytes)\n",
                    static_cast<unsigned long long>(scratchNeeded));
            return false;
        }
        m_scratchSize[m_slot] = scratchNeeded;
    }

    D3D12_GPU_VIRTUAL_ADDRESS scratchAddress = m_scratch[m_slot]->GetGPUVirtualAddress();
    scratchAddress = (scratchAddress + alignment - 1) & ~(alignment - 1);

    buildDesc.DestAccelerationStructureData = m_storage[m_slot]->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = scratchAddress;

    ID3D12GraphicsCommandList* frameList = immediate ? nullptr : commandListHandler.CurrentGfxList();
    if (frameList) {
        ComPtr<ID3D12GraphicsCommandList4> list = RayTracingList(frameList);
        if (not list)
            return false;
        list->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = m_storage[m_slot].Get();
        list->ResourceBarrier(1, &barrier);
    }
    else {
        CommandList* cl = commandListHandler.CreateCmdList("AccelerationStructure::BuildTopLevel", true);
        if (not (cl and cl->Open()))
            return false;
        ComPtr<ID3D12GraphicsCommandList4> list = RayTracingList(cl->GfxList());
        if (list)
            list->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        cl->Flush();
        if (not list)
            return false;
    }

    m_address = m_storage[m_slot]->GetGPUVirtualAddress();
    m_primitives = written;
    return true;
}

// =================================================================================================

void AccelerationStructure::Move(AccelerationStructure& other) noexcept
{
    m_vertices = std::move(other.m_vertices);
    m_indices = std::move(other.m_indices);
    m_address = other.m_address;
    m_primitives = other.m_primitives;
    m_slot = other.m_slot;
    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        m_storage[i] = std::move(other.m_storage[i]);
        m_storageSize[i] = other.m_storageSize[i];
        m_instances[i] = std::move(other.m_instances[i]);
        m_instanceCapacity[i] = other.m_instanceCapacity[i];
        m_scratch[i] = std::move(other.m_scratch[i]);
        m_scratchSize[i] = other.m_scratchSize[i];

        other.m_storageSize[i] = 0;
        other.m_instanceCapacity[i] = 0;
        other.m_scratchSize[i] = 0;
    }

    other.m_address = 0;
    other.m_primitives = 0;
    other.m_slot = 0;
}


bool AccelerationStructure::Bind(void) const noexcept
{
    if (not IsValid())
        return false;
    commandListHandler.BindAccelerationStructure(Handle());
    return true;
}


void AccelerationStructure::Destroy(void) noexcept
{
    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        ReleaseBuffer(m_storage[i]);
        m_storageSize[i] = 0;
        ReleaseUpload(m_instances[i]);
        m_instanceCapacity[i] = 0;
        ReleaseBuffer(m_scratch[i]);
        m_scratchSize[i] = 0;
    }
    ReleaseUpload(m_vertices);
    ReleaseUpload(m_indices);
    m_slot = 0;
    m_address = 0;
    m_primitives = 0;
}

// =================================================================================================
