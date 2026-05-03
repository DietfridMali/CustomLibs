#pragma once

#include <cstdint>
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

    virtual void* GfxDataBuffer(void) noexcept { return nullptr; }

    virtual uint32_t GfxDataLength(void) const noexcept { return 0; }

    virtual uint32_t GfxDataSize(void) const noexcept { return 0; }

    inline uint32_t ComponentCount(void) const noexcept {
        return m_componentCount;
    }

    inline bool IsDirty(bool setDirty = false) noexcept {
        if (setDirty)
            m_isDirty = true;
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
        AutoArray<GL_DATA_T>     m_gfxData;

#pragma warning(push)
#pragma warning(disable:4100)
        VertexDataBuffer(uint32_t componentCount = 1, size_t listSegmentSize = 1)
#pragma warning(pop)
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
        inline void SetGLData(AutoArray<GL_DATA_T>& glData) { // directly set m_gfxData without going over m_appData
            m_gfxData = glData;
            m_isDirty = true;
        }


		inline void CopyGLData(AutoArray<APP_DATA_T>& appData) { // directly set m_gfxData without going over m_appData
			m_gfxData.Resize(appData.Length() * m_componentCount);
			GL_DATA_T* glData = m_gfxData.Data();
            for (auto data : appData) {
                memcpy(glData, data.Data(), m_componentCount * sizeof(GL_DATA_T));
				glData += m_componentCount;
            }
            m_isDirty = true;
        }


        virtual AutoArray<GL_DATA_T>& Setup(void) = 0;


        inline void Reset(void) {
            m_appData.Clear();
            m_gfxData.Reset();
            m_isDirty = false;
        }


        inline operator void*() {
            return (void*)m_gfxData.data();
        }

        inline SegmentedList<APP_DATA_T>& AppData(void) noexcept {
            return m_appData;
        }

        inline AutoArray<GL_DATA_T>& GfxData(void) noexcept {
            return m_gfxData;
        }

        virtual void* GfxDataBuffer(void) noexcept override {
            return reinterpret_cast<void*>(m_gfxData.Data());
        }

        virtual uint32_t GfxDataLength(void) const noexcept override {
            return m_gfxData.Length();
        }

        virtual uint32_t GfxDataSize(void) const noexcept override {
            return m_gfxData.Length() * sizeof(GL_DATA_T);
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

        inline bool HaveGfxData(void) const {
            return m_gfxData.Length() > 0;
        }

        inline bool HaveData(void) const {
            return HaveAppData() or HaveGfxData();
        }

		inline bool IsEmpty(void) {
			return m_appData.IsEmpty();
		}

        inline void SetComponentCount(uint32_t componentCount) noexcept {
            m_componentCount = componentCount;
        }

        ~VertexDataBuffer () {
            Destroy ();
        }

protected:
    VertexDataBuffer& Copy(VertexDataBuffer const& other) {
        if ((this != &other) and other.HaveData() and (other.m_componentCount > 0)) {
            Reset();
            m_appData = other.m_appData;
            m_gfxData = other.m_gfxData;
			m_isDirty = other.m_isDirty;
            m_componentCount = other.m_componentCount;
        }
        return *this;
    }

    VertexDataBuffer& Move(VertexDataBuffer& other) {
        if ((this != &other) and other.HaveData() and (other.m_componentCount > 0)) {
            Reset();
            m_appData = std::move(other.m_appData);
            m_gfxData = std::move(other.m_gfxData);
            m_isDirty = other.m_isDirty;
            m_componentCount = other.m_componentCount;
            other.m_componentCount = 0;
        }
        return *this;
    }

public:
    VertexDataBuffer(const VertexDataBuffer& other)
        : BaseVertexDataBuffer(other.m_componentCount)
    {
        Copy(other);
    }

    VertexDataBuffer(VertexDataBuffer&& other) noexcept
        : BaseVertexDataBuffer(other.m_componentCount)
    {
        Move(other);
    }
};

// =================================================================================================
// Buffer for vertex data (4D xyzw vector of type numpy.float32). Also used for normal data.
// A pre-populated data buffer can be passed to the constructor

class VertexBuffer
    : public VertexDataBuffer <Vector3f, float> {
    public:
        VertexBuffer(size_t listSegmentSize = 1)
            : VertexDataBuffer(3, listSegmentSize)
        { }

        // Create a densely packed numpy array from the vertex data
        virtual AutoArray<float>& Setup(void) {
            if (HaveAppData()) {
                m_gfxData.Resize(m_appData.Length() * 3);
                float* glData = m_gfxData.Data();
                for (auto& v : m_appData) {
                    memcpy(glData, v.Data(), v.DataSize());
                    glData += 3;
                }
            }
            return m_gfxData;
        }
};

// =================================================================================================
// Buffer for texture coordinate data (2D uv vector). Also used for color information
// A pre-populated data buffer can be passed to the constructor

class TexCoordBuffer
    : public VertexDataBuffer <TexCoord, float> {
public:
        TexCoordBuffer(size_t listSegmentSize = 1)
            : VertexDataBuffer(2, listSegmentSize)
        { }

        // Create a densely packed numpy array from the vertex data
        virtual AutoArray<float>& Setup(void) {
            if (HaveAppData()) {
                float* glData = m_gfxData.Resize(m_appData.Length() * 2);
                for (auto& v : m_appData) {
                    memcpy(glData, v.Data(), v.DataSize());
                    glData += 2;
                }
            }
            return m_gfxData;
        }
};

// =================================================================================================
// Buffer for vertex data (4D xyzw vector of type numpy.float32). Also used for normal data.
// A pre-populated data buffer can be passed to the constructor

class TangentBuffer
    : public VertexDataBuffer <Vector4f, float> {
public:
    TangentBuffer(size_t listSegmentSize = 1)
        : VertexDataBuffer(4, listSegmentSize)
    {
    }

    // Create a densely packed numpy array from the vertex data
    virtual AutoArray<float>& Setup(void) {
        if (HaveAppData()) {
            m_gfxData.Resize(m_appData.Length() * 4);
            float* glData = m_gfxData.Data();
            for (auto& v : m_appData) {
                memcpy(glData, v.Data(), v.DataSize());
                glData += 4;
            }
        }
        return m_gfxData;
    }
};

// =================================================================================================
// Buffer for color data (4D rgba vector of type numpy.float32). 
// A pre-populated data buffer can be passed to the constructor

class ColorBuffer
    : public VertexDataBuffer <RGBAColor, float> {
public:
    ColorBuffer(size_t listSegmentSize = 1)
        : VertexDataBuffer(4, listSegmentSize)
    { }

    // Create a densely packed numpy array from the vertex data
    virtual AutoArray<float>& Setup(void) {
        if (HaveAppData()) {
            float* glData = m_gfxData.Resize(m_appData.Length() * 4);
            for (auto& v : m_appData) {
                memcpy(glData, v.Data(), v.DataSize());
                glData += 4;
            }
        }
        return m_gfxData;
    }
};

// =================================================================================================
// Buffer for index data (n-tuples of integer values). 
// Requires an additional componentCount parameter, as index count depends on the vertex count of the 
// primitive being rendered (quad: 4, triangle: 3, line: 2, point: 1)

class IndexBuffer
    : public VertexDataBuffer <AutoArray<uint32_t>, uint32_t> {
    public:
    IndexBuffer(uint32_t componentCount = 1, uint32_t listSegmentSize = 1)
        : VertexDataBuffer(componentCount, listSegmentSize)
    { }

    // Create a densely packed array from the vertex data
    virtual AutoArray<uint32_t>& Setup(void) {
        if (HaveAppData()) {
            uint32_t* glData = m_gfxData.Resize(m_appData.Length() * m_componentCount);
            for (auto& v : m_appData) {
                memcpy(glData, v.Data(), v.DataSize());
                glData += v.Length();
            }
        }
        return m_gfxData;
    }

    IndexBuffer& operator= (IndexBuffer const& other) {
        Copy(other);
        return *this;
    }
};

// =================================================================================================
// Buffer for index data (n-tuples of integer values). 
// Requires an additional componentCount parameter, as index count depends on the vertex count of the 
// primitive being rendered (quad: 4, triangle: 3, line: 2, point: 1)

class FloatDataBuffer
    : public VertexDataBuffer <float, float> {
public:
    FloatDataBuffer(uint32_t componentCount = 1, uint32_t listSegmentSize = 1)
        : VertexDataBuffer(componentCount, listSegmentSize)
    {
    }

    // Create a densely packed numpy array from the vertex data
    virtual AutoArray<float>& Setup(void) {
        if (HaveAppData()) {
            float* glData = m_gfxData.Resize(m_appData.Length() * m_componentCount);
            for (auto& v : m_appData) {
                *glData++ = v;
            }
        }
        return m_gfxData;
    }

    FloatDataBuffer& operator= (FloatDataBuffer const& other) {
        Copy(other);
        return *this;
    }
};

// =================================================================================================
