#pragma once

#include "framework.h"
#include "list.hpp"
#include "rendertypes.h"
#include "vertexdatabuffers.h"
#include "vbo.h"
#include "vector.hpp"
#include "texture.h"
#include "shader.h"

// =================================================================================================
// DX12 VAO
//
// In OpenGL a "Vertex Array Object" stored the vertex format bindings (VBO + attribute pointer
// layout) so that a single glBindVertexArray call reinstated everything.
// In DX12 there is no VAO equivalent.  This class manages the same collection of VBO data streams
// and delegates to the command list via IASetVertexBuffers / IASetIndexBuffer / DrawIndexedInstanced
// inside Enable() and Render().

class VAO
{
public:
    List<VBO*>          m_dataBuffers;
    VBO                 m_indexBuffer;
    MeshTopology        m_shape{ MeshTopology::Quads };
    bool                m_isDynamic{ false };
    bool                m_isBound{ false };

    static VAO*         activeVAO;
    static List<VAO*>   vaoStack;

    VAO() = default;

    static inline void PushVAO(VAO* vao) noexcept { vaoStack.Append(vao); }

    static inline VAO* PopVAO(void) noexcept {
        if (!vaoStack.Length()) return nullptr;
        VAO* vao = nullptr;
        vaoStack.Pop(vao);
        return vao;
    }

    inline void SetDynamic(bool isDynamic) noexcept {
        m_isDynamic = isDynamic;
        for (auto vbo : m_dataBuffers) vbo->SetDynamic(isDynamic);
        m_indexBuffer.SetDynamic(isDynamic);
    }

    inline void SetShape(MeshTopology shape) noexcept { m_shape = shape; }

    // In DX12 there is nothing to initialise at "VAO creation" time.
    // Returns true always.
    bool Create(MeshTopology shape = MeshTopology::Quads, bool isDynamic = false) noexcept;

    ~VAO() { Destroy(); }

    VAO(VAO const& other)  { Copy(other); }
    VAO(VAO&& other) noexcept { Move(other); }
    VAO& operator=(const VAO& other) { return Copy(other); }
    VAO& operator=(VAO&& other) noexcept { return Move(other); }

    VAO& Copy(VAO const& other);
    VAO& Move(VAO& other) noexcept;

    void Destroy(void) noexcept;

    inline bool IsValid(void)   noexcept { return true; }  // always "valid" in DX12
    inline bool IsBound(void)   noexcept { return m_isBound; }
    inline bool IsActive(void)  noexcept { return this == activeVAO; }

    inline void Activate(void) noexcept {
        if (!IsActive()) { PushVAO(activeVAO); activeVAO = this; }
    }

    inline void Deactivate(void) noexcept {
        if (IsActive()) {
            activeVAO = PopVAO();
            if (activeVAO && activeVAO->IsBound())
                activeVAO->Enable();
        }
    }

    // Bind vertex + index buffers on the command list.
    bool Enable(void) noexcept;
    void Disable(void) noexcept;

    inline bool EnableTextures(std::span<Texture* const> textures = {}) noexcept {
        int tmu = 0;
        for (Texture* tex : textures) {
            if (tex && !tex->Enable(tmu)) return false;
            ++tmu;
        }
        return true;
    }

    inline void DisableTextures(std::span<Texture* const> textures = {}) noexcept {
        for (Texture* tex : textures)
            if (tex) tex->Disable();
    }

    VBO* FindBuffer(const char* type, int id, int& index) noexcept;

    bool UpdateDataBuffer(const char* type, int id,
                          BaseVertexDataBuffer& buffer, ComponentType componentType,
                          bool forceUpdate = false) noexcept;

    void UpdateIndexBuffer(IndexBuffer& buffer, ComponentType componentType,
                           bool forceUpdate = false) noexcept;

    void Render(std::span<Texture* const> textures = {}) noexcept;

    inline void Render(Texture* texture) {
        Render(texture ? std::span<Texture* const>(&texture, 1) : std::span<Texture* const>{});
    }

protected:
    bool UpdateBuffer(const char* type, int id, void* data, size_t dataSize,
                      size_t componentType, size_t componentCount = 0,
                      bool forceUpdate = false) noexcept;

    bool UpdateDataBuffer(const char* type, int id, void* data, size_t dataSize,
                          size_t componentType, size_t componentCount,
                          bool forceUpdate = false) noexcept;

    void UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType,
                           bool forceUpdate = false) noexcept;
};

// =================================================================================================
