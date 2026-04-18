
#include "gfxdatalayout.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"

// =================================================================================================

static GLenum ToGLenum(MeshTopology topology) noexcept {
    switch (topology) {
        case MeshTopology::Triangles: return GL_TRIANGLES;
        case MeshTopology::Lines:     return GL_LINES;
        case MeshTopology::Points:    return GL_POINTS;
        case MeshTopology::Quads:     return GL_QUADS; // should not reach glDraw* — quads are converted to triangles in mesh.cpp
        default:                      return GL_TRIANGLES;
    }
}

static GLenum ToGLenum(ComponentType ct) noexcept {
    return ct == ComponentType::UInt32 ? GL_UNSIGNED_INT : GL_FLOAT;
}

// =================================================================================================

GfxDataLayout* GfxDataLayout::activeLayout = 0;
List<GfxDataLayout*> GfxDataLayout::layoutStack;

// =================================================================================================
// Interface to OpenGL VAOs

bool GfxDataLayout::Create(MeshTopology shape, bool isDynamic)
noexcept
{
    m_shape = shape;
    SetDynamic(isDynamic);
#if USE_SHARED_HANDLES
    if (m_handle.IsAvailable())
        return true;
    m_handle = SharedGfxHandle(0, glGenVertexArrays, glDeleteVertexArrays); // need to set allocate and release functions
    if (m_handle.Claim() == 0)
        return false;
    return true;
#else
    if (m_handle)
        return true;
    glGenVertexArrays(1, &m_handle);
    return m_handle != 0;
#endif
}


void GfxDataLayout::Destroy(void)
noexcept
{
    Disable();
    for (auto& gfxDataBuffer : m_dataBuffers) {
        gfxDataBuffer->Destroy();
        delete gfxDataBuffer;
    }
    m_indexBuffer.Destroy();
    m_dataBuffers.Clear();
#if USE_SHARED_HANDLES
    if (m_handle.IsAvailable())
        m_handle.Release();
#else
    if (m_handle) {
        glDeleteVertexArrays(1, &m_handle);
        m_handle = 0;
    }
#endif
}


GfxDataLayout& GfxDataLayout::Copy(GfxDataLayout const& other) {
    if (this != &other) {
        Destroy();
        m_dataBuffers = other.m_dataBuffers;
        m_indexBuffer = other.m_indexBuffer;
        m_handle = other.m_handle;
        m_shape = other.m_shape;
    }
    return *this;
}


GfxDataLayout& GfxDataLayout::Move(GfxDataLayout& other)
noexcept
{
    if (this != &other) {
        Destroy();
        m_dataBuffers = std::move(other.m_dataBuffers);
        m_indexBuffer = std::move(other.m_indexBuffer);
#if USE_SHARED_HANDLES
        m_handle = std::move(other.m_handle);
#else
        m_handle = other.m_handle;
        other.m_handle = 0;
#endif
        m_shape = other.m_shape;
    }
    return *this;
}


GfxDataBuffer* GfxDataLayout::FindBuffer(const char* type, int id, int& index)
noexcept
{
    int i = 0;
    for (auto gfxDataBuffer : m_dataBuffers) {
        if (gfxDataBuffer->IsType(type) and gfxDataBuffer->HasID(id)) {
            index = i;
            return gfxDataBuffer;
        }
        ++i;
    }
    return nullptr;
}

bool GfxDataLayout::UpdateDataBuffer(const char* type, int id, BaseVertexDataBuffer& buffer, ComponentType componentType, bool forceUpdate) noexcept {
    if (forceUpdate or buffer.IsDirty()) {
        if (not UpdateDataBuffer(type, id, buffer.GLDataBuffer(), buffer.GLDataSize(), ToGLenum(componentType), size_t(buffer.ComponentCount()), forceUpdate))
            return false;
        buffer.SetDirty(false);
    }
    return true;
}

void GfxDataLayout::UpdateIndexBuffer(IndexBuffer& buffer, ComponentType componentType, bool forceUpdate) noexcept {
    if (forceUpdate or buffer.IsDirty()) {
        UpdateIndexBuffer(buffer.GLDataBuffer(), buffer.GLDataSize(), ToGLenum(componentType), forceUpdate);
        buffer.SetDirty(false);
    }
}

// add a vertex or index data buffer
bool GfxDataLayout::UpdateBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate)
noexcept
{
    if (strcmp(type, "Index"))
        return UpdateDataBuffer(type, id, data, dataSize, componentType, componentCount, forceUpdate);
    UpdateIndexBuffer(data, dataSize, componentType);
    return true;
}


bool GfxDataLayout::UpdateDataBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate)
noexcept
{
    if (dataSize == 0)
        return false;
    bool disabled = not IsActive() or not IsBound();
    if (disabled)
        Enable();

    int index;
    GfxDataBuffer* buffer = FindBuffer(type, id, index);
    if (not buffer and (buffer = new GfxDataBuffer())) { // otherwise index has been initialized by FindBuffer()
        m_dataBuffers.Append(buffer);
        buffer->SetDynamic(m_isDynamic);
        index = m_dataBuffers.Length() - 1;
    }
    if (buffer)
        buffer->Update(type, GL_ARRAY_BUFFER, index, data, dataSize, componentType, componentCount, forceUpdate);

    if (disabled)
        Disable();
    return buffer != nullptr;
}


void GfxDataLayout::UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType, bool forceUpdate)
noexcept
{
    bool disabled = not IsActive() or not IsBound();
    if (disabled)
        Enable();
    m_indexBuffer.Update("Index", GL_ELEMENT_ARRAY_BUFFER, -1, data, dataSize, componentType, 1, forceUpdate);
    if (disabled)
        Disable();
}


bool GfxDataLayout::Enable(void)
noexcept
{
    if (not m_handle.IsAvailable())
        return false;
    Activate();
    if (not IsBound()) {
#if USE_SHARED_HANDLES
        glBindVertexArray(m_handle);
        m_isBound = true;
#else
        glBindVertexArray(m_handle);
        m_isBound = true; // BUGFIX: m_isBound wurde im !USE_SHARED_HANDLES-Zweig nicht gesetzt
#endif
    }
    return true;
}


void GfxDataLayout::Disable(void)
noexcept
{
    Deactivate();
    if (IsBound()) {
        glBindVertexArray(0);
        m_isBound = false;
    }
}

#ifdef _DEBUG

bool checkLayout = false;

static void DumpGfxData(GLuint gfxDataBufferId, int elemSize, const char* label) {
    std::cout << "=== gfxDataBufferId Dump: " << label << " (ID: " << gfxDataBufferId << ") ===" << std::endl;

    glBindBuffer(GL_ARRAY_BUFFER, gfxDataBufferId);
    GLint bufSize;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufSize);

    // gfxDataBufferId Gr��e checken
    GLint size;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
    std::cout << "Buffer size: " << size << " bytes" << std::endl;

    // Daten auslesen (z.B. erste 'count' floats)
    int count = bufSize / elemSize;
    std::vector<float> data(count);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), data.data());

    for (int i = 0; i < count; i++) {
        std::cout << "  [" << i << "] = " << data[i] << std::endl;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


static void CheckLayout(GLuint handle, const char* label = "") {
#if 1
    std::cout << "=== GfxDataLayout Check: " << label << " (ID: " << handle << ") ===" << std::endl;
#endif
    glBindVertexArray(handle);

    // Array Buffer Binding (sollte normalerweise 0 sein wenn GfxDataLayout korrekt setup)
    GLint arrayBuffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
#if 1
    std::cout << "GL_ARRAY_BUFFER_BINDING: " << arrayBuffer << std::endl;
#endif
    // Element Buffer (wichtig f�r indexed drawing)
    GLint elementBuffer;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
#if 1
    std::cout << "GL_ELEMENT_ARRAY_BUFFER_BINDING: " << elementBuffer << std::endl;
#endif
    // Attribute 0-3 checken (Position, TexCoord, Normal, etc.)
    for (int i = 0; i < 7; i++) {
        GLint enabled, size, type, stride, bufferBinding;
        GLvoid* pointer;

        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        if (enabled) {
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &bufferBinding);
            glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
#if 1
            std::cout << "  Attr " << i << ": enabled, size=" << size
                << ", type=0x" << std::hex << type << std::dec
                << ", stride=" << stride
                << ", gfxDataBufferId=" << bufferBinding
                << ", offset=" << (size_t)pointer << std::endl;
            DumpGfxData(bufferBinding, size, (size == 4) ? "color/tangents" : (size == 3) ? "vertices" : "texCoord");
#endif
            }
    #if 1
            else {
                std::cout << "  Attr " << i << ": DISABLED" << std::endl;
            }
    #endif
    }

    glBindVertexArray(0);
    baseRenderer.ClearGfxError();
}

#endif

void GfxDataLayout::Render(std::span<Texture* const> textures)
noexcept
{
#ifdef _DEBUG
    float* data = (float*)(m_dataBuffers[0]->m_data);
     if (checkLayout)
        CheckLayout(m_handle);
#endif
    if (not StartRender())
        return;
#if 1
    if (baseShaderHandler.ShaderIsActive() and (textures.size() > 0) and not EnableTextures(textures))
        return;
#endif
#if 1
    if (m_indexBuffer.m_data)
        glDrawElements(ToGLenum(m_shape), m_indexBuffer.m_itemCount, m_indexBuffer.m_componentType, nullptr); // draw using an index buffer
    else
        glDrawArrays(ToGLenum(m_shape), 0, m_dataBuffers[0]->m_itemCount); // draw non indexed arrays
#endif
    DisableTextures(textures);
    FinishRender();
}

// =================================================================================================
