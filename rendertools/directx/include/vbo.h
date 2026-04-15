#pragma once

#include "framework.h"
#include "rendertypes.h"
#include <cstring>
#include <vector>

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
    GfxBufferTarget          m_bufferType;     // Vertex or Index
    char*                    m_data;           // system-memory mirror (not used in DX12)

    ComPtr<ID3D12Resource>   m_resource;       // committed buffer on upload heap
    D3D12_VERTEX_BUFFER_VIEW m_vbv{};          // valid when m_bufferType == GfxBufferTarget::Vertex
    D3D12_INDEX_BUFFER_VIEW  m_ibv{};          // valid when m_bufferType == GfxBufferTarget::Index

    // Per-frame-slot chunk pool for dynamic multi-buffering.
    // FRAME_COUNT slots (indexed by cmdQueue.FrameIndex()) each hold a growing list of
    // upload-heap buffers. On each new recording session the slot's chunk index resets to 0,
    // which is safe because BeginFrame has already waited for the fence that covers the
    // previous use of this slot (2 frames ago with FRAME_COUNT=2).
    static constexpr UINT    FRAME_COUNT = 2;
    std::vector<ComPtr<ID3D12Resource>> m_chunks[FRAME_COUNT];
    int                      m_chunkIndex[FRAME_COUNT]{};
    uint64_t                 m_lastCmdListId[FRAME_COUNT]{};
    uint64_t                 m_lastExecutionId[FRAME_COUNT]{};

    uint32_t                 m_size;           // total buffer size in bytes
    size_t                   m_itemSize;       // bytes per vertex element (stride)
    uint32_t                 m_itemCount;
    int                      m_componentCount;
    ComponentType            m_componentType;  // Float / UInt32 / UInt16
    bool                     m_isDynamic;

    VBO(const char* type = "", int id = 0, GfxBufferTarget bufferType = GfxBufferTarget::Vertex, bool isDynamic = true) noexcept;

    void Reset(void) {
        m_resource.Reset();
        for (UINT i = 0; i < FRAME_COUNT; ++i)
            m_chunks[i].clear();
        m_vbv = {};
        m_ibv = {};
        m_id  = 0;
        m_isDynamic = true;
    }

    VBO(VBO const& other) { 
        Copy(other); 
    }
    
    VBO& operator=(VBO const& other) { 
        Copy(other); 
        return *this; 
    }
    
    VBO& operator=(VBO&& other) noexcept { 
        Move(other); 
        return *this; 
    }

    VBO& Copy(VBO const& other);
    VBO& Move(VBO& other) noexcept;

    // No-ops — binding handled by VAO::Enable() in DX12.
    inline void Bind(void) noexcept {}
    inline void Release(void) noexcept {}
    inline void EnableAttribs(void) noexcept {}
    inline void DisableAttribs(void) noexcept {}
    inline void Describe(void) noexcept {}

    bool Create(ID3D12Device* device, size_t dataSize);


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

    // Returns stride (bytes per vertex element) — used by VAO when building VBVs.
    inline UINT Stride() const noexcept { 
        return UINT(m_itemSize); 
    }

    // Returns true when the GPU resource exists and is ready to bind.
    inline bool IsValid() const noexcept { 
        return m_resource != nullptr; 
    }
};

// =================================================================================================
