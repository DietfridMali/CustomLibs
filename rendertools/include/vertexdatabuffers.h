#pragma once

#include "glew.h"
#include "vector.hpp"
#include "array.hpp"
#include "list.hpp"
#include "segmentedlist.hpp"
#include "texcoord.h"
#include "colordata.h"

// =================================================================================================
// Data buffer handling as support for vertex buffer operations.
// Interface classes between python and OpenGL representations of rendering data
// Supplies iterators, assignment and indexing operatores and transparent data conversion to OpenGL
// ready format (Setup() method)

class BaseVertexDataBuffer {
public:
    uint32_t    m_componentCount;
    bool        m_isDirty;

    BaseVertexDataBuffer(uint32_t componentCount = 1)
        : m_componentCount(componentCount), m_isDirty(false)
    { }

    ~BaseVertexDataBuffer() = default;

    virtual void* GLDataBuffer(void) noexcept { return nullptr; }

    virtual uint32_t GLDataLength(void) const noexcept { return 0; }

    virtual uint32_t GLDataSize(void) const noexcept { return 0; }

    inline uint32_t ComponentCount(void) const noexcept {
        return m_componentCount;
    }

    inline const bool IsDirty(void) const noexcept {
        return m_isDirty;
    }

    inline void SetDirty(bool isDirty) noexcept {
        m_isDirty = isDirty;
    }
};

// =================================================================================================

template < typename APP_DATA_T, typename GL_DATA_T>
class VertexDataBuffer 
    : public BaseVertexDataBuffer
{
    protected:
        SegmentedList<APP_DATA_T>   m_appData;
        ManagedArray<GL_DATA_T>     m_glData;

        VertexDataBuffer(uint32_t componentCount = 1, size_t listSegmentSize = 1)
            : BaseVertexDataBuffer(componentCount)
        {
#if USE_SEGMENTED_LISTS
            m_appData = SegmentedList<APP_DATA_T>(listSegmentSize);
#endif
        }

        VertexDataBuffer& operator=(const VertexDataBuffer& other) {
            return Copy(other);
        }

        VertexDataBuffer& operator= (VertexDataBuffer& other) {
            return Copy (other);
        }

        VertexDataBuffer& operator= (VertexDataBuffer&& other) noexcept {
            return Move(other);
        }

public:
        inline void SetGLData(ManagedArray<GL_DATA_T>& glData) { // directly set m_glData without going over m_appData
            m_glData = glData;
            m_isDirty = true;
        }

        virtual ManagedArray<GL_DATA_T>& Setup(void) = 0;

        inline void Reset(void) {
            m_appData.Clear();
            m_glData.Reset();
            m_isDirty = false;
        }

        inline operator GLvoid*() {
            return (GLvoid*)m_glData.data();
        }

        inline SegmentedList<APP_DATA_T>& AppData(void) noexcept {
            return m_appData;
        }

        inline ManagedArray<GL_DATA_T>& GLData(void) noexcept {
            return m_glData;
        }

        virtual void* GLDataBuffer(void) noexcept override {
            return reinterpret_cast<void*>(m_glData.Data());
        }

        virtual uint32_t GLDataLength(void) const noexcept override {
            return m_glData.Length();
        }

        virtual uint32_t GLDataSize(void) const noexcept override {
            return m_glData.Length() * sizeof(GL_DATA_T);
        }

        inline uint32_t AppDataLength(void) const noexcept {
            return m_appData.Length();
        }

        inline bool Append(APP_DATA_T data) {
            if (not m_appData.Append(data))
                return false;
            m_isDirty = true;
            return true;
        }

        inline bool Append(const SegmentedList<APP_DATA_T>& data) {
            m_isDirty = true;
            m_appData += data;
            return true;
        }

        inline APP_DATA_T& operator[] (const int32_t i) {
            return m_appData[i];
        }

        inline void Destroy (void) {
           Reset();
        }
        
        inline bool HaveAppData(void) const {
            return !m_appData.IsEmpty();
        }

        inline bool HaveGLData(void) const {
            return m_glData.Length() > 0;
        }

        inline bool HaveData(void) const {
            return HaveAppData() or HaveGLData();
        }

		inline bool IsEmpty(void) {
			return m_appData.IsEmpty();
		}

        ~VertexDataBuffer () {
            Destroy ();
        }

protected:
    VertexDataBuffer& Copy(VertexDataBuffer const& other) {
        if (this != &other) {
            Reset();
            if (other.HaveData()) {
                m_appData = other.m_appData;
                m_glData = other.m_glData;
            }
            m_componentCount = other.m_componentCount;
        }
        return *this;
    }

    VertexDataBuffer& Move(VertexDataBuffer& other) {
        if (this != &other) {
            Reset();
            if (other.HaveData()) {
                m_appData = std::move(other.m_appData);
                m_glData = std::move(other.m_glData);
            }
            m_componentCount = other.m_componentCount;
            other.m_componentCount = 0;
        }
        return *this;
    }
};

// =================================================================================================
// Buffer for vertex data (4D xyzw vector of type numpy.float32). Also used for normal data.
// A pre-populated data buffer can be passed to the constructor

class VertexBuffer : public VertexDataBuffer <Vector3f, GLfloat> {
    public:
        VertexBuffer(size_t listSegmentSize = 1) 
            : VertexDataBuffer(3, listSegmentSize) 
        { }

        // Create a densely packed numpy array from the vertex data
        virtual ManagedArray<GLfloat>& Setup(void) {
            if (HaveAppData()) {
                m_glData.Resize(m_appData.Length() * 3);
                GLfloat* glData = m_glData.Data();
                for (auto& v : m_appData) {
                    memcpy(glData, v.Data(), v.DataSize());
                    glData += 3;
                }
            }
            return m_glData;
        }
};

// =================================================================================================
// Buffer for texture coordinate data (2D uv vector). Also used for color information
// A pre-populated data buffer can be passed to the constructor

class TexCoordBuffer : public VertexDataBuffer <TexCoord, GLfloat> {
public:
        TexCoordBuffer(size_t listSegmentSize = 1) 
            : VertexDataBuffer(2, listSegmentSize) 
        { }

        // Create a densely packed numpy array from the vertex data
        virtual ManagedArray<GLfloat>& Setup(void) {
            if (HaveAppData()) {
                GLfloat* glData = m_glData.Resize(m_appData.Length() * 2);
                for (auto& v : m_appData) {
                    memcpy(glData, v.Data(), v.DataSize());
                    glData += 2;
                }
            }
            return m_glData;
        }
};

// =================================================================================================
// Buffer for color data (4D rgba vector of type numpy.float32). 
// A pre-populated data buffer can be passed to the constructor

class ColorBuffer : public VertexDataBuffer <RGBAColor, GLfloat> {
public:
    ColorBuffer(size_t listSegmentSize = 1) 
        : VertexDataBuffer(4, listSegmentSize) 
    { }

    // Create a densely packed numpy array from the vertex data
    virtual ManagedArray<GLfloat>& Setup(void) {
        if (HaveAppData()) {
            float* glData = m_glData.Resize(m_appData.Length() * 4);
            for (auto& v : m_appData) {
                memcpy(glData, v.Data(), v.DataSize());
                glData += 4;
            }
        }
        return m_glData;
    }
};

// =================================================================================================
// Buffer for index data (n-tuples of integer values). 
// Requires an additional componentCount parameter, as index count depends on the vertex count of the 
// primitive being rendered (quad: 4, triangle: 3, line: 2, point: 1)

class IndexBuffer : public VertexDataBuffer <ManagedArray<GLuint>, GLuint> {
    public:
    IndexBuffer(uint32_t componentCount = 1, uint32_t listSegmentSize = 1) 
        : VertexDataBuffer(componentCount, listSegmentSize) 
    { }

    // Create a densely packed numpy array from the vertex data
    virtual ManagedArray<GLuint>& Setup(void) {
        if (HaveAppData()) {
            uint32_t* glData = m_glData.Resize(m_appData.Length() * m_componentCount);
            for (auto& v : m_appData) {
                memcpy(glData, v.Data(), v.DataSize());
                glData += v.Length();
            }
        }
        return m_glData;
    }

    IndexBuffer& operator= (IndexBuffer const& other) {
        Copy (other);
        return *this;
    }

};

// =================================================================================================
