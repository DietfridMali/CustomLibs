
#include "vao.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"

// =================================================================================================

VAO* VAO::activeVAO = 0;
List<VAO*> VAO::vaoStack;

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

bool VAO::Create(GLuint shape, bool isDynamic)
noexcept
{
    m_shape = shape;
    SetDynamic(isDynamic);
#if USE_SHARED_HANDLES
    if (m_handle.IsAvailable())
        return true;
    m_handle = SharedGLHandle(0, glGenVertexArrays, glDeleteVertexArrays); // need to set allocate and release functions
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


void VAO::Destroy(void)
noexcept
{
    Disable();
    for (auto& vbo : m_dataBuffers) {
        vbo->Destroy();
        delete vbo;
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


VAO& VAO::Copy(VAO const& other) {
    if (this != &other) {
        Destroy();
        m_dataBuffers = other.m_dataBuffers;
        m_indexBuffer = other.m_indexBuffer;
        m_handle = other.m_handle;
        m_shape = other.m_shape;
    }
    return *this;
}


VAO& VAO::Move(VAO& other)
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


VBO* VAO::FindBuffer(const char* type, int id, int& index)
noexcept
{
    int i = 0;
    for (auto vbo : m_dataBuffers) {
        if (vbo->IsType(type) and vbo->HasID(id)) {
            index = i;
            return vbo;
        }
        ++i;
    }
    return nullptr;
}

// add a vertex or index data buffer
bool VAO::UpdateBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount)
noexcept
{
    if (strcmp(type, "Index"))
        return UpdateDataBuffer(type, id, data, dataSize, componentType, componentCount);
    UpdateIndexBuffer(data, dataSize, componentType);
    return true;
}


bool VAO::UpdateDataBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount)
noexcept
{
    bool disabled = not IsActive() or not IsBound();
    if (disabled)
        Enable();

    int index;
    VBO* vbo = FindBuffer(type, id, index);
    if (not vbo and (vbo = new VBO())) { // otherwise index has been initialized by FindBuffer()
        m_dataBuffers.Append(vbo);
        vbo->SetDynamic(m_isDynamic);
        index = m_dataBuffers.Length() - 1;
    }
    if (vbo)
        vbo->Update(type, GL_ARRAY_BUFFER, index, data, dataSize, componentType, componentCount);

    if (disabled)
        Disable();
    return vbo != nullptr;
}


void VAO::UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType)
noexcept
{
    bool disabled = not IsActive() or not IsBound();
    if (disabled)
        Enable();
    m_indexBuffer.Update("Index", GL_ELEMENT_ARRAY_BUFFER, -1, data, dataSize, componentType);
    if (disabled)
        Disable();
}


bool VAO::Enable(void)
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


void VAO::Disable(void)
noexcept
{
    Deactivate();
    if (IsBound()) {
        glBindVertexArray(0);
        m_isBound = false;
    }
}

#ifdef _DEBUG

bool checkVAO = false;

static void DumpVBO(GLuint vbo, int count, const char* label) {
    std::cout << "=== VBO Dump: " << label << " (ID: " << vbo << ") ===" << std::endl;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // VBO Größe checken
    GLint size;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);
    std::cout << "Buffer size: " << size << " bytes" << std::endl;

    // Daten auslesen (z.B. erste 'count' floats)
    std::vector<float> data(count);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), data.data());

    for (int i = 0; i < count; i++) {
        std::cout << "  [" << i << "] = " << data[i] << std::endl;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


static void CheckVAO(GLuint handle, const char* label = "") {
#if 1
    std::cout << "=== VAO Check: " << label << " (ID: " << handle << ") ===" << std::endl;
#endif
    glBindVertexArray(handle);

    // Array Buffer Binding (sollte normalerweise 0 sein wenn VAO korrekt setup)
    GLint arrayBuffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
#if 1
    std::cout << "GL_ARRAY_BUFFER_BINDING: " << arrayBuffer << std::endl;
#endif
    // Element Buffer (wichtig für indexed drawing)
    GLint elementBuffer;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
#if 1
    std::cout << "GL_ELEMENT_ARRAY_BUFFER_BINDING: " << elementBuffer << std::endl;
#endif
    // Attribute 0-3 checken (Position, TexCoord, Normal, etc.)
    for (int i = 0; i < 4; i++) {
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
                << ", VBO=" << bufferBinding
                << ", offset=" << (size_t)pointer << std::endl;
            DumpVBO(bufferBinding, 4 * size, (size == 3) ? "vertices" : "texCoord");
#endif
        }
#if 1
        else {
            std::cout << "  Attr " << i << ": DISABLED" << std::endl;
        }
#endif
    }

    glBindVertexArray(0);
    baseRenderer.ClearGLError();
}

#endif

void VAO::Render(Texture* texture)
noexcept
{
#ifdef _DEBUG
    float* data = (float*)(m_dataBuffers[0]->m_data);
    if (checkVAO)
        CheckVAO(m_handle);
#endif
    if (not Enable())
        return;
#if 1
    if (baseShaderHandler.ShaderIsActive() and texture and not EnableTexture(texture))
        return;
#endif
#if 1
    if (m_indexBuffer.m_data)
        glDrawElements(m_shape, m_indexBuffer.m_itemCount, m_indexBuffer.m_componentType, nullptr); // draw using an index buffer
    else
        glDrawArrays(m_shape, 0, m_dataBuffers[0]->m_itemCount); // draw non indexed arrays
#endif
    Disable();
    DisableTexture(texture);
}

// =================================================================================================
