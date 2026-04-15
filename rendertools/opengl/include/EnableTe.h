#pragma once

#include "glew.h"
#include "list.hpp"
#include "sharedglhandle.hpp"
#include "vertexdatabuffers.h"
#include "gfxDataBuffer.h"
#include "vector.hpp"
#include "texture.h"
#include "shader.h"

// =================================================================================================

#ifdef USE_SHARED_HANDLES
#   undef USE_SHARED_HANDLES
#endif

#define USE_SHARED_HANDLES 1

class GfxDataLayout
{
public:
    List<GfxDataBuffer*>          m_dataBuffers;
    GfxDataBuffer                 m_indexBuffer;
#if USE_SHARED_HANDLES
    SharedGLHandle      m_handle;
#else
    GLuint              m_handle;
#endif
    GLenum              m_shape{ GL_QUADS };
    bool                m_isDynamic{ false };
    bool                m_isBound{ false };

    static GfxDataLayout*         activeLayout;
    static List<GfxDataLayout*>   layoutStack;

    GfxDataLayout() = default;

    static inline void PushGfxDataLayout(GfxDataLayout* gfxDataLayout)
        noexcept
    {
        layoutStack.Append(gfxDataLayout);
    }

    static inline GfxDataLayout* PopGfxDataLayout(void)
        noexcept
    {
        if (not layoutStack.Length())
            return nullptr;
        GfxDataLayout* gfxDataLayout = nullptr;
        layoutStack.Pop(gfxDataLayout);
        return gfxDataLayout;
    }

    inline void SetDynamic(bool isDynamic)
        noexcept
    {
        m_isDynamic = isDynamic;
        for (auto gfxDataBuffer : m_dataBuffers)
            gfxDataBuffer->SetDynamic(isDynamic);
        m_indexBuffer.SetDynamic(m_isDynamic);
    }

    inline void SetShape(GLenum shape) noexcept {
        m_shape = shape;
    }

    bool Create(GLuint shape = GL_QUADS, bool isDynamic = false)
        noexcept;

    ~GfxDataLayout() {
        Destroy();
    }

    GfxDataLayout(GfxDataLayout const& other) {
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

    GfxDataLayout& Move(GfxDataLayout& other)
        noexcept;

    void Destroy(void)
        noexcept;

    void Reset(void)
        noexcept;

    inline bool IsValid(void)
        noexcept
    {
#if USE_SHARED_HANDLES
        return m_handle.IsAvailable();
#else
        return m_handle != 0;
#endif
    }

    inline bool IsBound(void)
        noexcept
    {
        return IsValid() and m_isBound;
    }

    inline bool IsActive(void)
        noexcept
    {
        return this == activeLayout;
    }

    inline void Activate(void)
        noexcept
    {
        if (not IsActive()) {
            PushGfxDataLayout(activeLayout);
            activeLayout = this;
        }
    }

    inline void Deactivate(void)
        noexcept
    {
        if (IsActive()) {
            activeLayout = PopGfxDataLayout();
            if (activeLayout and activeLayout->IsBound())
                activeLayout->Enable();
        }
    }

    bool Enable(void)
        noexcept;

    void Disable(void)
        noexcept;

    inline bool EnableTextures(std::span<Texture* const> textures)
        noexcept
    {
        int tmu = 0;
        for (Texture* texture : textures)
            if (texture != nullptr)
                if (not texture->Enable(tmu++))
                    return false;
        return true;
    }

    inline void DisableTextures(std::span<Texture* const> textures)
        noexcept
    {
        for (Texture* texture : textures)
            if (texture != nullptr)
                texture->Disable();
    }

    GfxDataBuffer* FindBuffer(const char* type, int id, int& index)
        noexcept;

    bool UpdateDataBuffer(const char* type, int id, BaseVertexDataBuffer& buffer, size_t componentType, bool forceUpdate = false) noexcept {
        if (forceUpdate or buffer.IsDirty()) {
            if (not UpdateDataBuffer(type, id, buffer.GLDataBuffer(), buffer.GLDataSize(), componentType, size_t(buffer.ComponentCount()), forceUpdate))
                return false;
            buffer.SetDirty(false);
        }
        return true;
    }
        

    void UpdateIndexBuffer(IndexBuffer& buffer, size_t componentType, bool forceUpdate = false) noexcept {
        if (forceUpdate or buffer.IsDirty()) {
            UpdateIndexBuffer(buffer.GLDataBuffer(), buffer.GLDataSize(), componentType, forceUpdate);
            buffer.SetDirty(false);
        }
    }

    void Render(std::span<Texture* const> textures = {})
        noexcept;

    protected:
        // add a vertex or index data buffer
        bool UpdateBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount = 0, bool forceUpdate = false)
            noexcept;

        bool UpdateDataBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate = false)
            noexcept;

        void UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType, bool forceUpdate = false)
            noexcept;
};

// =================================================================================================
