#pragma once

#include "framework.h"
#include "opengl_states.h"
#include <cstring>

// =================================================================================================
// DX12 VBO — wraps a committed GPU resource (upload heap) and exposes vertex / index buffer views.
//
// In the OGL version the VBO held a GLuint (via SharedGLHandle) and was described with
// glVertexAttribPointer / glBindBuffer.  In DX12 we use:
//   • A committed resource on the upload heap (CPU-writable, GPU-readable).
//   • D3D12_VERTEX_BUFFER_VIEW for vertex attribute streams (GL_ARRAY_BUFFER).
//   • D3D12_INDEX_BUFFER_VIEW  for the index stream (GL_ELEMENT_ARRAY_BUFFER).
//
// Bind() / Release() / EnableAttribs() / DisableAttribs() / Describe() are kept as no-ops so
// that VAO code compiles unchanged.  The actual IASetVertexBuffers / IASetIndexBuffer calls are
// emitted by VAO::Enable().

class VBO
{
public:
    int                      m_index;          // vertex attribute slot (layout location)
    const char*              m_type;           // debug tag ("vertices", "normals", …)
    int                      m_id;
    GLenum                   m_bufferType;     // GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER
    char*                    m_data;           // system-memory mirror (not used in DX12)

    ComPtr<ID3D12Resource>   m_resource;       // committed buffer on upload heap
    D3D12_VERTEX_BUFFER_VIEW m_vbv{};          // valid when m_bufferType == GL_ARRAY_BUFFER
    D3D12_INDEX_BUFFER_VIEW  m_ibv{};          // valid when m_bufferType == GL_ELEMENT_ARRAY_BUFFER

    GLsizei                  m_size;           // total buffer size in bytes
    size_t                   m_itemSize;       // bytes per vertex element (stride)
    GLsizei                  m_itemCount;
    int                      m_componentCount;
    GLenum                   m_componentType;  // GL_FLOAT / GL_UNSIGNED_INT / GL_UNSIGNED_SHORT
    bool                     m_isDynamic;

    VBO(const char* type = "", int id = 0,
        GLenum bufferType = GL_ARRAY_BUFFER, bool isDynamic = true) noexcept;

    void Reset(void) {
        m_resource.Reset();
        m_vbv = {};
        m_ibv = {};
        m_id  = 0;
        m_isDynamic = true;
    }

    VBO(VBO const& other)            { Copy(other); }
    VBO& operator=(VBO const& other) { Copy(other); return *this; }
    VBO& operator=(VBO&& other) noexcept { Move(other); return *this; }

    VBO& Copy(VBO const& other);
    VBO& Move(VBO& other) noexcept;

    // No-ops — binding handled by VAO::Enable() in DX12.
    inline void Bind(void)            noexcept {}
    inline void Release(void)         noexcept {}
    inline void EnableAttribs(void)   noexcept {}
    inline void DisableAttribs(void)  noexcept {}
    inline void Describe(void)        noexcept {}

    // Upload new data and (re-)create the GPU resource if needed.
    // componentType : GL_FLOAT | GL_UNSIGNED_INT | GL_UNSIGNED_SHORT
    // componentCount: components per vertex element (e.g. 3 for float3)
    bool Update(const char* type, GLenum bufferType, int index,
                void* data, size_t dataSize,
                size_t componentType, size_t componentCount,
                bool forceUpdate = false) noexcept;

    void Destroy(void) noexcept;

    size_t ComponentSize(size_t componentType) noexcept;

    inline bool IsType(const char* type)  noexcept { return !strcmp(m_type, type); }
    inline bool HasID(int id)             noexcept { return m_id == id; }
    inline void SetDynamic(bool d)        noexcept { m_isDynamic = d; }

    // Returns stride (bytes per vertex element) — used by VAO when building VBVs.
    inline UINT Stride() const noexcept { return UINT(m_itemSize); }

    // Returns true when the GPU resource exists and is ready to bind.
    inline bool IsValid() const noexcept { return m_resource != nullptr; }
};

// =================================================================================================
