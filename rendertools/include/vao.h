#pragma once

#include "glew.h"
#include "list.hpp"
#include "sharedglhandle.hpp"
#include "vbo.h"
#include "vector.hpp"
#include "texture.h"
#include "shader.h"

// =================================================================================================
// "Premium version of" OpenGL vertex array objects. CVAO instances offer methods to convert python
// lists into the corresponding lists of OpenGL items (vertices, normals, texture coordinates, etc)
// The current implementation requires a fixed order of array buffer creation to comply with the 
// corresponding layout positions in the shaders implemented here.
// Currently offers shaders for cubemap and regular (2D) texturing.
// Implements loading of varying textures, so an application item derived from or using a CVAO instance
// (e.g. an ico sphere) can be reused by different other application items that require different 
// texturing. This implementation basically allows for reusing one single ico sphere instance whereever
// a sphere is needed.
// Supports indexed and non indexed vertex buffer objects.
//
// // Due to the current shader implementation (fixed position layout), buffers need to be passed in a
// fixed sequence: vertices, colors, ...
// TODO: Expand shader for all kinds of inputs (texture coordinates, normals)
// See also https://qastack.com.de/programming/8704801/glvertexattribpointer-clarification

#ifdef USE_SHARED_HANDLES
#   undef USE_SHARED_HANDLES
#endif

#define USE_SHARED_HANDLES 1

class VAO
{
public:
    List<VBO*>          m_dataBuffers;
    VBO                 m_indexBuffer;
#if USE_SHARED_HANDLES
    SharedGLHandle      m_handle;
#else
    GLuint              m_handle;
#endif
    GLuint              m_shape;
    bool                m_isDynamic;
    bool                m_isBound;

    static VAO* activeVAO;
    static List<VAO*>   vaoStack;

    VAO(bool isDynamic = true)
        : m_isDynamic(isDynamic), m_isBound(false), m_shape(0)
#if USE_SHARED_HANDLES
        , m_handle(SharedGLHandle(0, glGenVertexArrays, glDeleteVertexArrays))
#else
        , m_handle(0)
#endif
    {
        SetDynamic(isDynamic);
    }

    static inline void PushVAO(VAO* vao)
        noexcept
    {
        vaoStack.Append(vao);
    }

    static inline VAO* PopVAO(void)
        noexcept
    {
        if (not vaoStack.Length())
            return nullptr;
        VAO* vao;
        vaoStack.Pop(vao);
        return vao;
    }

    inline void SetDynamic(bool isDynamic)
        noexcept
    {
        m_isDynamic = isDynamic;
        for (auto vbo : m_dataBuffers)
            vbo->SetDynamic(isDynamic);
        m_indexBuffer.SetDynamic(m_isDynamic);
    }

    bool Init(GLuint shape)
        noexcept;

    ~VAO() {
        Destroy();
    }

    VAO(VAO const& other) {
        Copy(other);
    }

    VAO(VAO&& other) noexcept {
        Move(other);
    }

    VAO& operator=(const VAO& other) {
        return Copy(other);
    }

    VAO& operator=(VAO&& other) noexcept {
        return Move(other);
    }

    VAO& Copy(VAO const& other);

    VAO& Move(VAO& other)
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
        return this == activeVAO;
    }

    inline void Activate(void)
        noexcept
    {
        if (not IsActive()) {
            PushVAO(activeVAO);
            activeVAO = this;
        }
    }

    inline void Deactivate(void)
        noexcept
    {
        if (IsActive()) {
            activeVAO = PopVAO();
            if (activeVAO and activeVAO->IsBound())
                activeVAO->Enable();
        }
    }

    void Enable(void)
        noexcept;

    void Disable(void)
        noexcept;

    inline Texture* EnableTexture(Texture* texture)
        noexcept
    {
        if (texture != nullptr)
            texture->Enable();
        return texture;
    }

    inline void DisableTexture(Texture* texture)
        noexcept
    {
        if (texture != nullptr)
            texture->Disable();
    }

    VBO* FindBuffer(const char* type, int& index)
        noexcept;

    // add a vertex or index data buffer
    bool UpdateBuffer(const char* type, void* data, size_t dataSize, size_t componentType, size_t componentCount = 0)
        noexcept;

    bool UpdateVertexBuffer(const char* type, void* data, size_t dataSize, size_t componentType, size_t componentCount)
        noexcept;

    void UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType)
        noexcept;

    void Render(Texture* texture = nullptr)
        noexcept;
};

// =================================================================================================
