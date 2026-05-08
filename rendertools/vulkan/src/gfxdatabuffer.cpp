#define NOMINMAX

#include "gfxdatabuffer.h"
#include "vkcontext.h"

#include <cstdio>
#include <cstring>

// =================================================================================================
// Vulkan GfxDataBuffer implementation
//
// Backing: GfxBuffer (VkBuffer + VmaAllocation) with HOST_ACCESS_SEQUENTIAL_WRITE + MAPPED.
// Both vertex and index buffers are host-visible — fine for dynamic mesh streaming. Static
// meshes could later be promoted to device-local + staged upload (Phase B step 12 territory).

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
        // VkBuffer / VmaAllocation are not refcounted — Copy creates an empty buffer; caller
        // must repopulate via Update.
        m_buffer = GfxBuffer { };
        m_size = 0;
        m_itemSize = other.m_itemSize;
        m_itemCount = 0;
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

        // Move the GfxBuffer by swapping fields (no copy semantics required).
        m_buffer.m_buffer = std::exchange(other.m_buffer.m_buffer, VkBuffer(VK_NULL_HANDLE));
        m_buffer.m_allocation = std::exchange(other.m_buffer.m_allocation, VmaAllocation(VK_NULL_HANDLE));
        m_buffer.m_size = std::exchange(other.m_buffer.m_size, VkDeviceSize(0));
        m_buffer.m_mapped = std::exchange(other.m_buffer.m_mapped, nullptr);

        m_size = other.m_size;
        m_itemSize = other.m_itemSize;
        m_itemCount = other.m_itemCount;
        m_componentCount = other.m_componentCount;
        m_componentType = other.m_componentType;
        m_isDynamic = other.m_isDynamic;
    }
    return *this;
}


bool GfxDataBuffer::Create(size_t dataSize)
{
    VkBufferUsageFlags usage = (m_bufferType == GfxBufferTarget::Index)
                             ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                             : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (not m_buffer.Create(VkDeviceSize(dataSize), usage,
                            VMA_MEMORY_USAGE_AUTO,
                            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                          | VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        fprintf(stderr, "GfxDataBuffer::Create: GfxBuffer::Create failed (size=%zu, type=%s/%d)\n",
                dataSize, m_type ? m_type : "?", m_id);
        return false;
    }
    return true;
}


bool GfxDataBuffer::Update(const char* type, GfxBufferTarget bufferType, int index,
                           void* data, size_t dataSize,
                           ComponentType componentType, size_t componentCount,
                           bool /*forceUpdate*/) noexcept
{
    if ((not data) or (dataSize == 0))
        return false;
    if (vkContext.Device() == VK_NULL_HANDLE)
        return false;

    m_type = type;
    m_bufferType = bufferType;
    m_index = index;
    m_componentType = componentType;
    m_componentCount = int(componentCount);

    m_itemSize = ComponentSize(size_t(componentType)) * componentCount;
    m_itemCount = uint32_t(dataSize / ((m_itemSize > 0) ? m_itemSize : 1));
    m_size = uint32_t(dataSize);

    if (m_isDynamic or (not m_buffer.IsValid()) or (m_buffer.Size() < dataSize)) {
        if (not Create(dataSize))
            return false;
    }

    return m_buffer.Upload(data, VkDeviceSize(dataSize), 0);
}


void GfxDataBuffer::Destroy(void) noexcept
{
    m_buffer.Destroy();
    m_size = 0;
    m_itemCount = 0;
    m_itemSize = 0;
}

// =================================================================================================
