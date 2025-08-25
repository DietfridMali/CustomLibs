#pragma once

#include "glew.h"
//#include <string.h>
#include "sharedpointer.hpp"
#include "sharedglhandle.hpp"

#ifdef USE_SHARED_HANDLES
#   undef USE_SHARED_HANDLES
#endif

#define USE_SHARED_HANDLES 1

// =================================================================================================
// OpenGL vertex buffer handling: Creation, sending attributes to OpenGL, binding for rendering

class VBO
{
public:

    int                 m_index;
    const char* m_type;
    GLenum              m_bufferType;
    char* m_data;
#if USE_SHARED_HANDLES
    SharedGLHandle      m_handle;
#else
    GLuint              m_handle;
#endif
    GLsizei             m_size;
    size_t              m_itemSize;
    GLsizei             m_itemCount;
    GLint               m_componentCount;
    GLenum              m_componentType;
    bool                m_isDynamic;

    VBO(const char* type = "", GLint bufferType = GL_ARRAY_BUFFER, bool isDynamic = true)
        noexcept;

    void Reset(void) {
#if USE_SHARED_HANDLES
        m_handle = SharedGLHandle();
#else
        m_handle = 0;
#endif
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

    VBO& Move(VBO& other)
        noexcept;

    inline void Bind(void)
        noexcept
    {
        glBindBuffer(m_bufferType, m_handle);
    }

    inline void Release(void)
        noexcept
    {
        glBindBuffer(m_bufferType, 0);
    }

    inline void EnableAttribs(void)
        noexcept
    {
        if (m_index > -1)
            glEnableVertexAttribArray(m_index);
    }

    inline void DisableAttribs(void)
        noexcept
    {
        if (m_index > -1)
            glDisableVertexAttribArray(m_index);
    }

    inline void Describe(void)
        noexcept
    {
        if (m_index > -1) {
            glVertexAttribPointer(m_index, m_componentCount, m_componentType, GL_FALSE, 0, nullptr);
            EnableAttribs();
        }
    }

    // data: buffer with OpenGL data (float or unsigned int)
    // dataSize: buffer size in bytes
    // componentType: OpenGL type of OpenGL data components (GL_FLOAT or GL_UNSIGNED_INT)
    // componentCount: Number of components of the primitives represented by the render data (3 for 3D vectors, 2 for texture coords, 4 for color values, ...)
    bool Update(const char* type, GLint bufferType, int index, void* data, size_t dataSize, size_t componentType, size_t componentCount = 1)
#if USE_SHARED_HANDLES
        noexcept(noexcept(Bind()) && noexcept(Describe()) && noexcept(m_handle.Claim()))
#else
        noexcept(noexcept(Bind()) && noexcept(Describe()))
#endif
        ;

    void Destroy(void)
        noexcept;

    size_t ComponentSize(size_t componentType)
        noexcept;

    inline bool IsType(const char* type)
        noexcept
    {
        return !strcmp(m_type, type);
    }

    inline void SetDynamic(bool isDynamic)
        noexcept
    {
        m_isDynamic = isDynamic;
    }
};

// =================================================================================================
