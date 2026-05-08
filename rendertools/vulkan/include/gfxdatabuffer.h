#pragma once

#include "vkframework.h"
#include "gfx_buffer.h"
#include "rendertypes.h"
#include "resource_descriptor.h"
#include <cstring>

// =================================================================================================
// Vulkan GfxDataBuffer — wraps a host-visible VkBuffer for vertex / index streams.
//
// In DX12 this held a committed upload-heap resource and a D3D12_VERTEX/INDEX_BUFFER_VIEW.
// In Vulkan we use:
//   • GfxBuffer (VkBuffer + VmaAllocation, persistent-mapped, host-visible) as the backing.
//   • Stride and item count exposed for pipeline-layout / draw-call use.
//   • No CPU-side mirror (m_data) needed — VMA persistent-mapping gives direct write access.
//
// Bind() / Release() / EnableAttribs() / DisableAttribs() / Describe() are kept as no-ops so
// that GfxDataLayout code compiles unchanged. The actual vkCmdBindVertexBuffers /
// vkCmdBindIndexBuffer calls are emitted by GfxDataLayout::Enable().

class GfxDataBuffer
    : public ResourceDescriptor
{
public:
    int                 m_index;          // vertex attribute slot (layout location)
    const char*         m_type;           // debug tag ("vertices", "normals", …)
    int                 m_id;
    GfxBufferTarget     m_bufferType;     // Vertex or Index
    char*               m_data;           // unused in Vulkan (kept for source compatibility)

    GfxBuffer           m_buffer;         // VkBuffer + VmaAllocation backing

    uint32_t            m_size;           // total buffer size in bytes
    size_t              m_itemSize;       // bytes per vertex element (stride)
    uint32_t            m_itemCount;
    int                 m_componentCount;
    ComponentType       m_componentType;  // Float / UInt32 / UInt16
    bool                m_isDynamic;

    GfxDataBuffer(const char* type = "", int id = 0, GfxBufferTarget bufferType = GfxBufferTarget::Vertex, bool isDynamic = true) noexcept;

    void Clear(void) {
        m_buffer.Destroy();
        m_id = 0;
        m_isDynamic = true;
    }

    GfxDataBuffer(GfxDataBuffer const& other) {
        Copy(other);
    }

    GfxDataBuffer& operator=(GfxDataBuffer const& other) {
        Copy(other);
        return *this;
    }

    GfxDataBuffer& operator=(GfxDataBuffer&& other) noexcept {
        Move(other);
        return *this;
    }

    GfxDataBuffer& Copy(GfxDataBuffer const& other);

    GfxDataBuffer& Move(GfxDataBuffer& other) noexcept;

    // No-ops — binding handled by GfxDataLayout::Enable() in Vulkan.
    inline void Bind(void) noexcept {}
    inline void Release(void) noexcept {}
    inline void EnableAttribs(void) noexcept {}
    inline void DisableAttribs(void) noexcept {}
    inline void Describe(void) noexcept {}

    bool Create(size_t dataSize);

    // Upload new data and (re-)create the GPU resource if needed.
    // componentCount: components per vertex element (e.g. 3 for float3)
    bool Update(const char* type, GfxBufferTarget bufferType, int index,
                void* data, size_t dataSize,
                ComponentType componentType, size_t componentCount,
                bool forceUpdate = false) noexcept;

    void Destroy(void) noexcept;

    size_t ComponentSize(size_t componentType) noexcept;

    inline bool IsType(const char* type) noexcept {
        return !strcmp(m_type, type);
    }

    inline bool HasID(int id) noexcept {
        return m_id == id;
    }

    inline void SetDynamic(bool d) noexcept {
        m_isDynamic = d;
    }

    inline uint32_t Stride() const noexcept {
        return uint32_t(m_itemSize);
    }

    inline VkBuffer Buffer() const noexcept {
        return m_buffer.Buffer();
    }

    // For index buffers: the matching VkIndexType (UINT16 or UINT32).
    inline VkIndexType IndexType() const noexcept {
        return (m_componentType == ComponentType::UInt16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    }

    inline bool IsValid() const noexcept {
        return m_buffer.IsValid();
    }
};

// =================================================================================================
