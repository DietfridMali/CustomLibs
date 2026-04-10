#pragma once

#include "vertexdatabuffers.h"

// =================================================================================================
// Data buffer handling as support for vertex buffer operations.
// Interface classes between python and OpenGL representations of rendering data
// Supplies iterators, assignment and indexing operatores and transparent data conversion to OpenGL
// ready format (Setup() method)

VertexDataBuffer& VertexDataBuffer::Copy(VertexDataBuffer const& other) {
    if (this != &other) {
        m_appData = other.m_appData;
        m_glData = other.m_glData;
        m_componentCount = other.m_componentCount;
    }
    return *this;
}

// =================================================================================================
// Buffer for vertex data (4D xyzw vector of type numpy.float32). Also used for normal data.
// A pre-populated data buffer can be passed to the constructor

Array<GLfloat>& VertexDataBuffer::Setup(void) {
    m_glData.Create(m_appData.Length() * 3);
    size_t n = 0;
    for (auto [i, v] : m_appData) {
        m_glData[n++] = v.m_data.x;
        m_glData[n++] = v.m_data.y;
        m_glData[n++] = v.m_data.z;
    }
    return m_glData;
}

// =================================================================================================
// Buffer for texture coordinate data (2D uv vector). Also used for color information
// A pre-populated data buffer can be passed to the constructor

// Create a densely packed numpy array from the vertex data
Array<GLfloat>& TexCoordBuffer::Setup(void) {
    m_glData.Create(m_appData.Length() * 2);
    size_t n = 0;
    for (auto [i, v] : m_appData) {
        m_glData[n++] = v.m_u;
        m_glData[n++] = v.m_v;
    }
    return m_glData;
}

// =================================================================================================
// Buffer for color data (4D rgba vector of type numpy.float32). 
// A pre-populated data buffer can be passed to the constructor

// Create a densely packed numpy array from the vertex data
Array<GLfloat>& ColorBuffer::Setup(void) {
    m_glData.Create(m_appData.Length() * 4);
    size_t n = 0;
    for (auto [i, v] : m_appData) {
        m_glData[n++] = v.m_data.x;
        m_glData[n++] = v.m_data.y;
        m_glData[n++] = v.m_data.z;
        m_glData[n++] = v.m_data.w;
    }
    return m_glData;
}

// =================================================================================================
// Buffer for index data (n-tuples of integer values). 
// Requires an additional componentCount parameter, as index count depends on the vertex count of the 
// primitive being rendered (quad: 4, triangle: 3, line: 2, point: 1)

// Create a densely packed numpy array from the vertex data
virtual Array<GLuint>& IndexBuffer::Setup(void) {
    m_glData.Create(m_appData.Length() * m_componentCount);
    size_t n = 0;
    for (auto [i, v] : m_appData)
        for (auto const& j : v) 
            m_glData[n++] = GLuint (*j);
    return m_glData;
}

IndexBuffer& IndexBuffer::operator= (IndexBuffer const& other) {
    Copy (other);
    return *this;

}

// =================================================================================================
