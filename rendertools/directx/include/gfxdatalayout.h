#pragma once

#include "framework.h"
#include "list.hpp"
#include "rendertypes.h"
#include "vertexdatabuffers.h"
#include "gfxdatabuffer.h"
#include "vector.hpp"
#include "texture.h"
#include "shader.h"
#include "commandlist.h"

// =================================================================================================
// DX12 GfxDataLayout
//
// In OpenGL a "Vertex Array Object" stored the vertex format bindings (GfxDataBuffer + attribute pointer
// layout) so that a single glBindVertexArray call reinstated everything.
// In DX12 there is no GfxDataLayout equivalent.  This class manages the same collection of GfxDataBuffer data streams
// and delegates to the command list via IASetVertexBuffers / IASetIndexBuffer / DrawIndexedInstanced
// inside Enable() and Render().

class GfxDataLayout
{
public:
    List<GfxDataBuffer*>    m_dataBuffers;
    GfxDataBuffer           m_indexBuffer;
    MeshTopology            m_shape{ MeshTopology::Quads };
	CommandList*            m_updateList{ nullptr };  
    bool                    m_isDynamic{ false };
    bool                    m_isBound{ false };

    static GfxDataLayout*         activeLayout;
    static List<GfxDataLayout*>   layoutStack;

    GfxDataLayout() = default;

    static inline void PushGfxDataLayout(GfxDataLayout* gfxDataLayout) noexcept { 
        layoutStack.Append(gfxDataLayout); 
    }

    static inline GfxDataLayout* PopGfxDataLayout(void) noexcept {
        if (not layoutStack.Length()) 
            return nullptr;
        GfxDataLayout* gfxDataLayout = nullptr;
        layoutStack.Pop(gfxDataLayout);
        return gfxDataLayout;
    }

    inline void SetDynamic(bool isDynamic) noexcept {
        m_isDynamic = isDynamic;
        for (auto GfxDataBuffer : m_dataBuffers) 
            GfxDataBuffer->SetDynamic(isDynamic);
        m_indexBuffer.SetDynamic(isDynamic);
    }

    inline void SetShape(MeshTopology shape) noexcept { m_shape = shape; }

    // In DX12 there is nothing to initialise at "GfxDataLayout creation" time.
    // Returns true always.
    bool Create(MeshTopology shape = MeshTopology::Quads, bool isDynamic = false) noexcept;

    ~GfxDataLayout() { Destroy(); }

    GfxDataLayout(GfxDataLayout const& other)  { 
        Copy(other); 
    }

    GfxDataLayout(GfxDataLayout&& other) noexcept { 
        Move(other); 
    }
    
    GfxDataLayout& operator=(const GfxDataLayout& other) { 
        return Copy(other); 
    }
    
    GfxDataLayout& operator=(GfxDataLayout&& other) noexcept { 
        return Move(other); 
    }

    GfxDataLayout& Copy(GfxDataLayout const& other);
    
    GfxDataLayout& Move(GfxDataLayout& other) noexcept;

    void Destroy(void) noexcept;

    inline bool IsValid(void) noexcept { 
        return true; 
    }  // always "valid" in DX12
    
    inline bool IsBound(void) noexcept { 
        return m_isBound; 
    }
    
    inline bool IsActive(void) noexcept { 
        return this == activeLayout; 
    }

    inline void Activate(void) noexcept {
        if (not IsActive()) { 
            PushGfxDataLayout(activeLayout); 
            activeLayout = this; 
        }
    }

    inline void Deactivate(void) noexcept {
        if (IsActive()) {
            activeLayout = PopGfxDataLayout();
            if (activeLayout && activeLayout->IsBound())
                activeLayout->Enable();
        }
    }

    // Bind vertex + index buffers on the command list.
    bool Enable(void) noexcept;

    void Disable(void) noexcept;

    bool StartUpdate(void) noexcept;

    void FinishUpdate(void) noexcept;

    inline bool StartRender(void) noexcept {
        return Enable(); 
    }

    inline void FinishRender(void) noexcept {
        Disable();
    }

    inline bool EnableTextures(std::span<Texture* const> textures = {}) noexcept {
        int tmu = 0;
        for (Texture* t : textures) {
            if (t && not t->Enable(tmu)) 
                return false;
            ++tmu;
        }
        return true;
    }

    inline void DisableTextures(std::span<Texture* const> textures = {}) noexcept {
        for (Texture* t : textures)
            if (t) 
                t->Disable();
    }

    GfxDataBuffer* FindBuffer(const char* type, int id, int& index) noexcept;

    bool UpdateDataBuffer(const char* type, int id, BaseVertexDataBuffer& buffer, ComponentType componentType, bool forceUpdate = false) noexcept;

    void UpdateIndexBuffer(IndexBuffer& buffer, ComponentType componentType, bool forceUpdate = false) noexcept;

    void Render(std::span<Texture* const> textures = {}) noexcept;

    inline void Render(Texture* texture) {
        Render(texture ? std::span<Texture* const>(&texture, 1) : std::span<Texture* const>{});
    }

protected:
    bool UpdateBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount = 0, bool forceUpdate = false) noexcept;  // componentType cast to ComponentType internally

    bool UpdateDataBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate = false) noexcept;

    bool UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType, bool forceUpdate = false) noexcept;
};

// =================================================================================================
