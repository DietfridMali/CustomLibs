#include "vbo.h"
#include "base_renderer.h"

// =================================================================================================
// OpenGL vertex buffer handling: Creation, sending attributes to OpenGL, binding for rendering

// data: buffer with OpenGL data (float or unsigned int)
// dataSize: buffer size in bytes
// componentType: OpenGL type of OpenGL data components (GL_FLOAT or GL_UNSIGNED_INT)
// componentCount: Number of components of the primitives represented by the render data (3 for 3D vectors, 2 for texture coords, 4 for color values, ...)
VBO::VBO(const char* type, GLint bufferType, bool isDynamic) noexcept
    , m_index(-1)
    , m_type(type)
    , m_bufferType(bufferType)
    , m_data(nullptr)
#if USE_SHARED_HANDLES
    , m_handle(SharedBufferHandle())
#else
    , m_handle(0)
#endif
    , m_size(0)
    , m_itemSize(0)
    , m_itemCount(0)
    , m_componentCount(0)
    , m_componentType(0)
    , m_isDynamic(isDynamic)
{ }


size_t VBO::ComponentSize(size_t componentType)
noexcept
{
    switch (componentType) {
    case GL_FLOAT:
        return 4;
    case GL_UNSIGNED_INT:
        return 4;
    case GL_UNSIGNED_SHORT:
        return 2;
    default:
        return 4;
    }
}


VBO& VBO::Copy(VBO const& other) {
    if (this != &other) {
        m_index = other.m_index;
        m_type = other.m_type;
        m_id = other.m_id;
        m_bufferType = other.m_bufferType;
        m_data = other.m_data;
        m_handle = other.m_handle;
        m_size = other.m_size;
        m_itemSize = other.m_itemSize;
        m_itemCount = other.m_itemCount;
        m_componentCount = other.m_componentCount;
        m_componentType = other.m_componentType;
        m_isDynamic = other.m_isDynamic;
    }
    return *this;
}


VBO& VBO::Move(VBO& other)
noexcept
{
    if (this != &other) {
        m_index = other.m_index;
        m_type = other.m_type;
        m_id = other.m_id;
        m_bufferType = other.m_bufferType;
        m_data = other.m_data;
        m_handle = std::move(other.m_handle);
#if !USE_SHARED_HANDLES
        other.m_handle = 0;
#endif
        m_size = other.m_size;
        m_itemSize = other.m_itemSize;
        m_itemCount = other.m_itemCount;
        m_componentCount = other.m_componentCount;
        m_componentType = other.m_componentType;
        m_isDynamic = other.m_isDynamic;
        other.Reset();
    }
    return *this;
}


bool VBO::Update(const char* type, GLint bufferType, int index, void* data, size_t dataSize, size_t componentType, size_t componentCount)
#if USE_SHARED_HANDLES
noexcept(noexcept(Bind()) && noexcept(Describe()) && noexcept(m_handle.Claim()))
#else
noexcept(noexcept(Bind()) && noexcept(Describe()))
#endif
{
    baseRenderer.ClearGLError();
    bool update;
#if USE_SHARED_HANDLES
    if (m_handle.IsAvailable()) {
#else
    if (m_handle != 0) {                     // BUGFIX: fehlende Klammer im #else-Zweig ergänzt
#endif
        if (m_isDynamic)
            update = m_size == dataSize;
        else {
            Bind();
            Describe();
            return true;
        }
    }
    else {
#if USE_SHARED_HANDLES
        if (not m_handle.Claim())
#else
        glGenBuffers(1, &m_handle);
        if (m_handle == 0)
#endif
            return false;

        update = false;
    }
    if (not update) {
        m_type = type;
        m_bufferType = bufferType;
        m_itemSize = ComponentSize(componentType) * componentCount;
        m_itemCount = GLsizei(dataSize / m_itemSize);
        m_componentType = GLenum(componentType);
        m_componentCount = GLint(componentCount);
    }
    m_index = index;
    m_data = reinterpret_cast<char*>(data);
    m_size = GLsizei(dataSize);
    Bind();
    if (m_isDynamic and update)
        glBufferSubData(m_bufferType, 0, dataSize, data);
    else
        glBufferData(m_bufferType, dataSize, data, m_isDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    Describe();
    return true;
    }


void VBO::Destroy(void)
noexcept
{
    if (m_handle) {
        Release();
#if USE_SHARED_HANDLES
        m_handle.Release();
#else
        if (m_handle) {
            glDeleteBuffers(1, &m_handle);
            m_handle = 0;
        }
#endif
    }
}

// =================================================================================================
