#include "vkframework.h"

#include "acceleration_structure.h"
#include "vkcontext.h"
#include "vkupload.h"
#include "commandlist.h"
#include "resource_handler.h"

#include <cstdio>
#include <cstring>
#include <vector>

// =================================================================================================

namespace
{
    PFN_vkCreateAccelerationStructureKHR            pfnCreateAccelerationStructure          { nullptr };
    PFN_vkDestroyAccelerationStructureKHR           pfnDestroyAccelerationStructure         { nullptr };
    PFN_vkGetAccelerationStructureBuildSizesKHR     pfnGetAccelerationStructureBuildSizes   { nullptr };
    PFN_vkCmdBuildAccelerationStructuresKHR         pfnCmdBuildAccelerationStructures       { nullptr };
    PFN_vkGetAccelerationStructureDeviceAddressKHR  pfnGetAccelerationStructureDeviceAddress{ nullptr };

    uint32_t g_scratchAlignment { 128 };
    bool     g_loaded           { false };

    VkDeviceAddress BufferAddress(VkBuffer buffer) noexcept
    {
        if (buffer == VK_NULL_HANDLE)
            return 0;
        VkBufferDeviceAddressInfo info { };
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = buffer;
        return vkGetBufferDeviceAddress(vkContext.Device(), &info);
    }

    constexpr VkBufferUsageFlags kBuildInputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                                  | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    constexpr VkBufferUsageFlags kStorageUsage    = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                                  | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    constexpr VkBufferUsageFlags kScratchUsage    = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                  | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    constexpr VmaAllocationCreateFlags kHostWrite = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                                  | VMA_ALLOCATION_CREATE_MAPPED_BIT;
}

// =================================================================================================

bool RayTracingApi::Load(VkDevice device, VkPhysicalDevice physicalDevice) noexcept
{
    if (g_loaded)
        return true;
    if ((device == VK_NULL_HANDLE) or (physicalDevice == VK_NULL_HANDLE))
        return false;

    pfnCreateAccelerationStructure = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>
        (vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
    pfnDestroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>
        (vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
    pfnGetAccelerationStructureBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>
        (vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
    pfnCmdBuildAccelerationStructures = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>
        (vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
    pfnGetAccelerationStructureDeviceAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>
        (vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));

    if (not (pfnCreateAccelerationStructure and pfnDestroyAccelerationStructure and
             pfnGetAccelerationStructureBuildSizes and pfnCmdBuildAccelerationStructures and
             pfnGetAccelerationStructureDeviceAddress)) {
        fprintf(stderr, "RayTracingApi::Load: acceleration structure entry points missing\n");
        return false;
    }

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelProps { };
    accelProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 props { };
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &accelProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &props);
    g_scratchAlignment = accelProps.minAccelerationStructureScratchOffsetAlignment;
    if (g_scratchAlignment == 0)
        g_scratchAlignment = 128;

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

// Everything one bottom level structure needs between "here is the geometry" and "record the
// build". Kept per item so a batch can prepare them all, then submit once.

namespace
{
    struct PreparedBuild
    {
        std::vector<VkAccelerationStructureGeometryKHR>       geoms;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
        std::vector<uint32_t>                                 primitiveCounts;
        VkAccelerationStructureBuildGeometryInfoKHR           buildInfo { };
        VkDeviceSize                                          scratchSize   { 0 };
        VkDeviceSize                                          scratchOffset { 0 };
        AccelerationStructure*                                target { nullptr };
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

    if (not as.m_vertices.Create(VkDeviceSize(totalVertices) * 3 * sizeof(float), kBuildInputUsage,
                                 VMA_MEMORY_USAGE_AUTO, kHostWrite)) {
        fprintf(stderr, "AccelerationStructure: vertex buffer allocation failed\n");
        as.Destroy();
        return false;
    }
    if (not as.m_indices.Create(VkDeviceSize(totalIndices) * sizeof(uint32_t), kBuildInputUsage,
                                VMA_MEMORY_USAGE_AUTO, kHostWrite)) {
        fprintf(stderr, "AccelerationStructure: index buffer allocation failed\n");
        as.Destroy();
        return false;
    }

    float* pVertex = static_cast<float*>(as.m_vertices.Mapped());
    uint32_t* pIndex = static_cast<uint32_t*>(as.m_indices.Mapped());
    if ((not pVertex) or (not pIndex)) {
        fprintf(stderr, "AccelerationStructure: build input buffers are not mapped\n");
        as.Destroy();
        return false;
    }

    std::vector<VkAccelerationStructureGeometryKHR>& geoms = out.geoms;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR>& ranges = out.ranges;
    std::vector<uint32_t>& primitiveCounts = out.primitiveCounts;

    geoms.resize(geometryCount);
    ranges.resize(geometryCount);
    primitiveCounts.resize(geometryCount);

    const VkDeviceAddress vertexBase = BufferAddress(as.m_vertices.Buffer());
    const VkDeviceAddress indexBase = BufferAddress(as.m_indices.Buffer());

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    for (uint32_t i = 0; i < geometryCount; ++i) {
        const AccelGeometryDesc& src = geometries[i];

        memcpy(pVertex + size_t(vertexOffset) * 3, src.vertices, size_t(src.vertexCount) * 3 * sizeof(float));
        // The indices of all geometries live in ONE buffer and ONE vertex range, so every index is
        // shifted by where this geometry's vertices begin.
        for (uint32_t j = 0; j < src.indexCount; ++j)
            pIndex[indexOffset + j] = src.indices[j] + vertexOffset;

        VkAccelerationStructureGeometryKHR& geom = geoms[i];
        geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geom.flags = src.opaque ? VkGeometryFlagsKHR(VK_GEOMETRY_OPAQUE_BIT_KHR) : VkGeometryFlagsKHR(0);
        geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geom.geometry.triangles.vertexData.deviceAddress = vertexBase;
        geom.geometry.triangles.vertexStride = 3 * sizeof(float);
        geom.geometry.triangles.maxVertex = totalVertices - 1;
        geom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geom.geometry.triangles.indexData.deviceAddress = indexBase + VkDeviceSize(indexOffset) * sizeof(uint32_t);
        geom.geometry.triangles.transformData.deviceAddress = 0;

        primitiveCounts[i] = src.indexCount / 3;
        ranges[i].primitiveCount = primitiveCounts[i];
        ranges[i].primitiveOffset = 0;
        ranges[i].firstVertex = 0;
        ranges[i].transformOffset = 0;

        as.m_primitives += primitiveCounts[i];
        vertexOffset += src.vertexCount;
        indexOffset += src.indexCount;
    }

    out.target = &as;
    out.buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    out.buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    out.buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    out.buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    out.buildInfo.geometryCount = geometryCount;

    VkDevice device = vkContext.Device();

    // pGeometries must point at the final storage, and the sizes query reads it - so it is set
    // here and set again before the build, after the item vector has stopped growing.
    out.buildInfo.pGeometries = geoms.data();

    VkAccelerationStructureBuildSizesInfoKHR sizes { };
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetAccelerationStructureBuildSizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                          &out.buildInfo, primitiveCounts.data(), &sizes);
    if (sizes.accelerationStructureSize == 0) {
        fprintf(stderr, "AccelerationStructure: build size is zero\n");
        as.Destroy();
        return false;
    }

    if (not as.m_storage[0].Create(sizes.accelerationStructureSize, kStorageUsage, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)) {
        fprintf(stderr, "AccelerationStructure: storage buffer allocation failed (%llu bytes)\n",
                static_cast<unsigned long long>(sizes.accelerationStructureSize));
        as.Destroy();
        return false;
    }

    VkAccelerationStructureCreateInfoKHR createInfo { };
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = as.m_storage[0].Buffer();
    createInfo.offset = 0;
    createInfo.size = sizes.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VkResult res = pfnCreateAccelerationStructure(device, &createInfo, nullptr, &as.m_handles[0]);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "AccelerationStructure: vkCreateAccelerationStructureKHR failed (%d)\n", int(res));
        as.m_handles[0] = VK_NULL_HANDLE;
        as.Destroy();
        return false;
    }

    out.buildInfo.dstAccelerationStructure = as.m_handles[0];
    as.m_storageSize[0] = sizes.accelerationStructureSize;
    out.scratchSize = sizes.buildScratchSize;
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

    const VkDeviceSize alignment = VkDeviceSize(RayTracingApi::ScratchAlignment());

    std::vector<PreparedBuild> prepared(itemCount);
    VkDeviceSize scratchTotal = 0;
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

    GfxBuffer scratch;
    if (not scratch.Create(scratchTotal + alignment, kScratchUsage, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)) {
        fprintf(stderr, "AccelerationStructure: scratch allocation failed (%llu bytes)\n",
                static_cast<unsigned long long>(scratchTotal));
        for (uint32_t i = 0; i < nPrepared; ++i)
            prepared[i].target->Destroy();
        return false;
    }
    VkDeviceAddress scratchBase = BufferAddress(scratch.Buffer());
    scratchBase = (scratchBase + alignment - 1) & ~(alignment - 1);

    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos(nPrepared);
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers(nPrepared);

    for (uint32_t i = 0; i < nPrepared; ++i) {
        PreparedBuild& p = prepared[i];
        p.buildInfo.pGeometries = p.geoms.data();
        p.buildInfo.scratchData.deviceAddress = scratchBase + p.scratchOffset;
        buildInfos[i] = p.buildInfo;
        rangePointers[i] = p.ranges.data();
    }

    OneShotCommandBuffer cmd;
    if (not BeginSingleTimeCommands(cmd)) {
        scratch.Destroy();
        for (uint32_t i = 0; i < nPrepared; ++i)
            prepared[i].target->Destroy();
        return false;
    }
    pfnCmdBuildAccelerationStructures(cmd.cb, nPrepared, buildInfos.data(), rangePointers.data());
    const bool submitted = EndSingleTimeCommands(cmd);
    scratch.Destroy();

    VkDevice device = vkContext.Device();
    for (uint32_t i = 0; i < nPrepared; ++i) {
        AccelerationStructure& as = *prepared[i].target;
        if (not submitted) {
            as.Destroy();
            continue;
        }
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo { };
        addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = as.m_handles[0];
        as.m_address = pfnGetAccelerationStructureDeviceAddress(device, &addressInfo);
        // The build has been waited for, and a structure built in BUILD mode never reads its input
        // again - only a refit would. What the rays traverse is m_storage.
        as.m_vertices.Destroy();
        as.m_indices.Destroy();
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

bool AccelerationStructure::BuildTopLevel(const AccelInstance* instances, uint32_t instanceCount) noexcept
{
    if (not RayTracingApi::IsLoaded())
        return false;
    if ((not instances) or (instanceCount == 0))
        return false;

    VkDevice device = vkContext.Device();

    // The slot is the FRAME's, not the call's. Tying it to the frame index is what makes the two
    // slots mean anything: the frame that last used this one has been waited for at BeginFrame,
    // while the other slot still belongs to the frame in flight. Counting calls instead would put
    // two rebuilds in one frame into different slots and overwrite what that frame is reading.
    m_slot = commandListHandler.CmdQueue().FrameIndex() % kFrameSlots;

    GfxBuffer& instanceBuffer = m_instances[m_slot];

    if (instanceCount > m_instanceCapacity[m_slot]) {
        instanceBuffer.Destroy();
        if (not instanceBuffer.Create(VkDeviceSize(instanceCount) * sizeof(VkAccelerationStructureInstanceKHR),
                                      kBuildInputUsage, VMA_MEMORY_USAGE_AUTO, kHostWrite)) {
            fprintf(stderr, "AccelerationStructure::BuildTopLevel: instance buffer allocation failed (%u)\n", instanceCount);
            m_instanceCapacity[m_slot] = 0;
            return false;
        }
        m_instanceCapacity[m_slot] = instanceCount;
    }

    VkAccelerationStructureInstanceKHR* pInstance =
        static_cast<VkAccelerationStructureInstanceKHR*>(instanceBuffer.Mapped());
    if (not pInstance) {
        fprintf(stderr, "AccelerationStructure::BuildTopLevel: instance buffer is not mapped\n");
        return false;
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        const AccelInstance& src = instances[i];
        if ((not src.blas) or (not src.blas->IsValid()))
            continue;
        VkAccelerationStructureInstanceKHR& dst = pInstance[written];
        memcpy(&dst.transform, src.transform, sizeof(float) * 12);
        dst.instanceCustomIndex = src.customIndex & 0xFFFFFFu;
        dst.mask = src.mask & 0xFFu;
        dst.instanceShaderBindingTableRecordOffset = 0;
        dst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        dst.accelerationStructureReference = uint64_t(src.blas->DeviceAddress());
        ++written;
    }
    if (written == 0)
        return false;

    VkAccelerationStructureGeometryKHR geom { };
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.flags = 0;
    geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.arrayOfPointers = VK_FALSE;
    geom.geometry.instances.data.deviceAddress = BufferAddress(instanceBuffer.Buffer());

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo { };
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geom;

    VkAccelerationStructureBuildRangeInfoKHR range { };
    range.primitiveCount = written;
    range.primitiveOffset = 0;
    range.firstVertex = 0;
    range.transformOffset = 0;

    VkAccelerationStructureBuildSizesInfoKHR sizes { };
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetAccelerationStructureBuildSizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                          &buildInfo, &written, &sizes);
    if (sizes.accelerationStructureSize == 0) {
        fprintf(stderr, "AccelerationStructure::BuildTopLevel: build size is zero\n");
        return false;
    }

    // THE REBUILD ALLOCATES NOTHING while the slot's structure is big enough. A build in BUILD mode
    // overwrites the structure completely, and it may be written into a structure created for any
    // size at or above what this build needs - so only growth costs a new one.
    if ((m_handles[m_slot] == VK_NULL_HANDLE) or (m_storageSize[m_slot] < sizes.accelerationStructureSize)) {
        if (m_handles[m_slot] != VK_NULL_HANDLE) {
            VkAccelerationStructureKHR oldHandle = m_handles[m_slot];
            gfxResourceHandler.TrackCleanup([oldHandle]() {
                if (pfnDestroyAccelerationStructure)
                    pfnDestroyAccelerationStructure(vkContext.Device(), oldHandle, nullptr);
            });
            m_handles[m_slot] = VK_NULL_HANDLE;
        }
        m_storage[m_slot].Destroy();
        m_storageSize[m_slot] = 0;
        if (not m_storage[m_slot].Create(sizes.accelerationStructureSize, kStorageUsage, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)) {
            fprintf(stderr, "AccelerationStructure::BuildTopLevel: storage allocation failed (%llu bytes)\n",
                    static_cast<unsigned long long>(sizes.accelerationStructureSize));
            return false;
        }

        VkAccelerationStructureCreateInfoKHR createInfo { };
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = m_storage[m_slot].Buffer();
        createInfo.offset = 0;
        createInfo.size = sizes.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        VkResult res = pfnCreateAccelerationStructure(device, &createInfo, nullptr, &m_handles[m_slot]);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "AccelerationStructure::BuildTopLevel: vkCreateAccelerationStructureKHR failed (%d)\n", int(res));
            m_handles[m_slot] = VK_NULL_HANDLE;
            m_storage[m_slot].Destroy();
            return false;
        }
        m_storageSize[m_slot] = sizes.accelerationStructureSize;
    }

    // Scratch is reused on the same terms. The address carries the alignment, not the buffer, so
    // the allocation is padded and the address rounded up inside it.
    const VkDeviceSize alignment = VkDeviceSize(RayTracingApi::ScratchAlignment());
    const VkDeviceSize scratchNeeded = sizes.buildScratchSize + alignment;

    if (m_scratchSize[m_slot] < scratchNeeded) {
        m_scratch[m_slot].Destroy();
        m_scratchSize[m_slot] = 0;
        if (not m_scratch[m_slot].Create(scratchNeeded, kScratchUsage, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)) {
            fprintf(stderr, "AccelerationStructure::BuildTopLevel: scratch allocation failed (%llu bytes)\n",
                    static_cast<unsigned long long>(scratchNeeded));
            return false;
        }
        m_scratchSize[m_slot] = scratchNeeded;
    }

    VkDeviceAddress scratchAddress = BufferAddress(m_scratch[m_slot].Buffer());
    scratchAddress = (scratchAddress + alignment - 1) & ~(alignment - 1);

    buildInfo.dstAccelerationStructure = m_handles[m_slot];
    buildInfo.scratchData.deviceAddress = scratchAddress;

    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

    // Whether there is a command buffer to record into - NOT whether a render pass is open. Those
    // are different questions, and IsInRendering () answers the second one: a build must not be
    // issued inside a vkCmdBeginRendering scope, and inside one a barrier may only name
    // framebuffer space stages. So the pass is suspended around the build and taken up again.
    VkCommandBuffer frameBuffer = commandListHandler.CurrentGfxList();
    if (frameBuffer != VK_NULL_HANDLE) {
        CommandListHandler::RenderingScope scope = commandListHandler.SuspendRendering();

        pfnCmdBuildAccelerationStructures(frameBuffer, 1, &buildInfo, &pRange);

        VkMemoryBarrier2 barrier { };
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

        VkDependencyInfo dependency { };
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frameBuffer, &dependency);

        commandListHandler.ResumeRendering(scope);
    }
    else {
        OneShotCommandBuffer cmd;
        if (not BeginSingleTimeCommands(cmd))
            return false;
        pfnCmdBuildAccelerationStructures(cmd.cb, 1, &buildInfo, &pRange);
        if (not EndSingleTimeCommands(cmd))
            return false;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo { };
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_handles[m_slot];
    m_address = pfnGetAccelerationStructureDeviceAddress(device, &addressInfo);
    m_primitives = written;
    return true;
}

// =================================================================================================

void AccelerationStructure::Move(AccelerationStructure& other) noexcept
{
    m_vertices = other.m_vertices;
    m_indices = other.m_indices;
    m_address = other.m_address;
    m_primitives = other.m_primitives;
    m_slot = other.m_slot;
    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        m_handles[i] = other.m_handles[i];
        m_storage[i] = other.m_storage[i];
        m_storageSize[i] = other.m_storageSize[i];
        m_instances[i] = other.m_instances[i];
        m_instanceCapacity[i] = other.m_instanceCapacity[i];
        m_scratch[i] = other.m_scratch[i];
        m_scratchSize[i] = other.m_scratchSize[i];

        other.m_handles[i] = VK_NULL_HANDLE;
        other.m_storage[i] = GfxBuffer();
        other.m_storageSize[i] = 0;
        other.m_instances[i] = GfxBuffer();
        other.m_instanceCapacity[i] = 0;
        other.m_scratch[i] = GfxBuffer();
        other.m_scratchSize[i] = 0;
    }

    other.m_vertices = GfxBuffer();
    other.m_indices = GfxBuffer();
    other.m_address = 0;
    other.m_primitives = 0;
    other.m_slot = 0;
}


void AccelerationStructure::Destroy(void) noexcept
{
    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        // Deferred like everything else here: Destroy may be called while a frame that traces
        // against this structure is still in flight. After the resource handler itself is gone,
        // TrackCleanup runs the callback inline, which is safe - the GPU is idle by then.
        if ((m_handles[i] != VK_NULL_HANDLE) and pfnDestroyAccelerationStructure) {
            VkAccelerationStructureKHR handle = m_handles[i];
            gfxResourceHandler.TrackCleanup([handle]() {
                if (pfnDestroyAccelerationStructure)
                    pfnDestroyAccelerationStructure(vkContext.Device(), handle, nullptr);
            });
        }
        m_handles[i] = VK_NULL_HANDLE;
        m_storage[i].Destroy();
        m_storageSize[i] = 0;
        m_instances[i].Destroy();
        m_instanceCapacity[i] = 0;
        m_scratch[i].Destroy();
        m_scratchSize[i] = 0;
    }
    m_vertices.Destroy();
    m_indices.Destroy();
    m_slot = 0;
    m_address = 0;
    m_primitives = 0;
}

// =================================================================================================
