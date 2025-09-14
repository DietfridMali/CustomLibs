#pragma once

#ifndef _TEXTURE_H
#   include "texture.h"
#endif

#include <cstdint>
#include <cstring>
#include <algorithm>

// inline file for header
// =================================================================================================

template<typename T> struct GLTexTraits;

template<> struct GLTexTraits<uint8_t> {
    static constexpr GLint  internalFormat = GL_R8;
    static constexpr GLenum format = GL_RED;
    static constexpr GLenum type = GL_UNSIGNED_BYTE;
    static constexpr GLint  align = 1;
};


template<> struct GLTexTraits<Vector4f> {
    static constexpr GLint  internalFormat = GL_RGBA32F;  // oder GL_RGBA16F / GL_RGBA8
    static constexpr GLenum format = GL_RGBA;
    static constexpr GLenum type = GL_FLOAT;
    static constexpr GLint  align = 4;
};


template <typename DATA_T>
class LinearTexture
    : public Texture
{
public:
    ManagedArray<DATA_T>    m_data;
    bool                    m_isRepeating;

    LinearTexture()
        : m_isRepeating(false)
    { }

    ~LinearTexture() = default;


    void SetParams(bool enforce) override {
        if (enforce || !m_hasParams) {
            m_hasParams = true;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_isRepeating ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }


    virtual void Deploy(int bufferIndex) override {
         if (!IsAvailable()) 
            return;
        Bind();
        glPixelStorei(GL_UNPACK_ALIGNMENT, GLTexTraits<DATA_T>::align);
        TextureBuffer* texBuf = m_buffers[0];
        glTexImage2D(
            GL_TEXTURE_2D, 0,
            GLTexTraits<DATA_T>::internalFormat,
            texBuf->m_info.m_width, 1, 0,
            GLTexTraits<DATA_T>::format,
            GLTexTraits<DATA_T>::type,
            nullptr);
        SetParams(false);
        Release();
    }


    inline bool Create(ManagedArray<DATA_T>& data, bool isRepeating) {
        if (not Texture::Create())
            return false;
        m_isRepeating = isRepeating;
        if (!Allocate(static_cast<int>(data.Length())))
            return false;
        Upload(data);
        return true;
    }


    bool Allocate(int length) {
        TextureBuffer* texBuf = new TextureBuffer();
        if (!texBuf) 
            return false;
        if (!m_buffers.Append(texBuf)) { 
            delete texBuf; 
            return false; 
        }
        texBuf->m_info = TextureBuffer::BufferInfo(length, 1, GLTexTraits<DATA_T>::align, GLTexTraits<DATA_T>::internalFormat, GLTexTraits<DATA_T>::format);
        HasBuffer() = true;
        Deploy(0);
        return true;
    }


    inline int Upload(ManagedArray<DATA_T>& data) {
        if (m_buffers.Length() == 0) 
            return 0;
        const int l = std::min(GetWidth(), int(data.Length()));
        if (l <= 0) 
            return 0;

        Bind();
        glPixelStorei(GL_UNPACK_ALIGNMENT, GLTexTraits<DATA_T>::align);
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, l, 1,
            GLTexTraits<DATA_T>::format,
            GLTexTraits<DATA_T>::type,
            data.Data());
        Release();
        return l;
    }
};

// =================================================================================================
