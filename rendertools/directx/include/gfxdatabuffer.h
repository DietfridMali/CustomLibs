#pragma once

#include "dx12framework.h"
#include "rendertypes.h"
#include "resource_descriptor.h"
#include <cstring>

// =================================================================================================
// DX12 GfxDataBuffer — wraps a committed GPU resource (upload heap) and exposes vertex / index buffer views.
//
// In the OGL version the GfxDataBuffer held a GLuint (via SharedGfxHandle) and was described with
// glVertexAttribPointer / glBindBuffer.  In DX12 we use:
//   • A committed resource on the upload heap (CPU-writable, GPU-readable).
//   • D3D12_VERTEX_BUFFER_VIEW for vertex attribute streams (GL_ARRAY_BUFFER).
//   • D3D12_INDEX_BUFFER_VIEW  for the index stream (GL_ELEMENT_ARRAY_BUFFER).
//
// Bind() / Release() / EnableAttribs() / DisableAttribs() / Describe() are kept as no-ops so
// that GfxDataLayout code compiles unchanged.  The actual IASetVertexBuffers / IASetIndexBuffer calls are
// emitted by GfxDataLayout::Enable().

class GfxDataBuffer
	: public ResourceDescriptor
{
public:
    int                      m_index;          // vertex attribute slot (layout location)
    const char*              m_type;           // debug tag ("vertices", "normals", …)
    int                      m_id;
    GfxBufferTarget          m_bufferType;     // Vertex or Index
    char*                    m_data;           // system-memory mirror (not used in DX12)

    ComPtr<ID3D12Resource>   m_resource;       // committed buffer on upload heap
    D3D12_VERTEX_BUFFER_VIEW m_vbv{};          // valid when m_bufferType == GfxBufferTarget::Vertex
    D3D12_INDEX_BUFFER_VIEW  m_ibv{};          // valid when m_bufferType == GfxBufferTarget::Index

    uint32_t                 m_size;           // total buffer size in bytes
    size_t                   m_itemSize;       // bytes per vertex element (stride)
    uint32_t                 m_itemCount;
    int                      m_componentCount;
    ComponentType            m_componentType;  // Float / UInt32 / UInt16
    bool                     m_isDynamic;

    GfxDataBuffer(const char* type = "", int id = 0, GfxBufferTarget bufferType = GfxBufferTarget::Vertex, bool isDynamic = true) noexcept;

    void Clear(void) {
        m_resource.Reset();
        m_vbv = {};
        m_ibv = {};
        m_id  = 0;
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

    // No-ops — binding handled by GfxDataLayout::Enable() in DX12.
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

    // Returns stride (bytes per vertex element) — used by GfxDataLayout when building VBVs.
    inline UINT Stride() const noexcept { 
        return UINT(m_itemSize); 
    }

    // Returns true when the GPU resource exists and is ready to bind.
    inline bool IsValid() const noexcept { 
        return m_resource != nullptr; 
    }
};

// =================================================================================================
