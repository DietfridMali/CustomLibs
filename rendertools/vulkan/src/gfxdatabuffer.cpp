#define NOMINMAX

#include "gfxdatabuffer.h"
#include "vkcontext.h"
#include "commandlist.h"
#include "resource_handler.h"

#include <cstdio>
#include <cstring>

static_assert(GfxDataBuffer::FRAME_COUNT == CommandQueue::FRAME_COUNT,
              "GfxDataBuffer upload-slot count must match the command-queue frame count");

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
        // VkBuffer / VmaAllocation are not refcounted — Copy creates empty buffers; caller
        // must repopulate via Update.
        for (auto& b : m_buffer)
            b = GfxBuffer { };
        m_activeSlot = 0;
        m_lastUpdateFrame = UINT64_MAX;
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

        // Move each GfxBuffer slot by transferring its handles, then null the source slot.
        for (int i = 0; i < FRAME_COUNT; ++i) {
            m_buffer[i] = other.m_buffer[i];
            other.m_buffer[i] = GfxBuffer { };
        }
        m_activeSlot = other.m_activeSlot;
        m_lastUpdateFrame = other.m_lastUpdateFrame;

        m_size = other.m_size;
        m_itemSize = other.m_itemSize;
        m_itemCount = other.m_itemCount;
        m_componentCount = other.m_componentCount;
        m_componentType = other.m_componentType;
        m_isDynamic = other.m_isDynamic;
    }
    return *this;
}


bool GfxDataBuffer::Create(int slot, size_t dataSize)
{
    VkBufferUsageFlags usage = (m_bufferType == GfxBufferTarget::Index)
                             ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                             : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (not m_buffer[slot].Create(VkDeviceSize(dataSize), usage,
                                  VMA_MEMORY_USAGE_AUTO,
                                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                | VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        fprintf(stderr, "GfxDataBuffer::Create: GfxBuffer::Create failed (size=%zu, type=%s/%d slot %d)\n",
                dataSize, m_type ? m_type : "?", m_id, slot);
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

    // Dynamic buffers rotate through FRAME_COUNT slots so a write never lands on a buffer the
    // GPU may still be reading from a previous in-flight frame; static buffers always use
    // slot 0. Each slot is created once and reused — no per-frame VkBuffer allocation.
    //
    // A second Update of the same buffer within one frame (e.g. MapLayout drawing every map's
    // layout during init, with no frame boundary between them) would overwrite a slot still
    // referenced by an already-recorded draw — so a same-frame re-update takes a fresh buffer
    // and defers the old one's destruction by one frame slot via gfxResourceHandler.
    const uint64_t frameNumber = commandListHandler.CmdQueue().FrameNumber();
    const int slot = m_isDynamic ? int(commandListHandler.CmdQueue().FrameIndex()) : 0;
    const bool sameFrameReupdate = (frameNumber == m_lastUpdateFrame);

    if (sameFrameReupdate and m_buffer[slot].IsValid()) {
        gfxResourceHandler.TrackCleanup([b = m_buffer[slot]]() mutable { b.Destroy(); });
        m_buffer[slot] = GfxBuffer { };
    }
    if (sameFrameReupdate or (not m_buffer[slot].IsValid()) or (m_buffer[slot].Size() < dataSize)) {
        m_buffer[slot].Destroy();
        if (not Create(slot, dataSize))
            return false;
    }
    m_activeSlot = slot;
    m_lastUpdateFrame = frameNumber;

    return m_buffer[slot].Upload(data, VkDeviceSize(dataSize), 0);
}


void GfxDataBuffer::Destroy(void) noexcept
{
    for (auto& b : m_buffer)
        b.Destroy();
    m_size = 0;
    m_itemCount = 0;
    m_itemSize = 0;
}

// =================================================================================================
