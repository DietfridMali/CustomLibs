#pragma once

#include "std_defines.h"

#include "glew.h"
#include "array.hpp"
#include "string.hpp"
#include "list.hpp"
#include "conversions.hpp"
#include "sharedpointer.hpp"
#include "sharedglhandle.hpp"
#include "avltree.hpp"
#include "opengl_states.h"

#include "SDL.h"
#include "SDL_image.h"

class Texture;

using TextureList = List<Texture*>;

#ifdef USE_SHARED_HANDLES
#   undef USE_SHARED_HANDLES
#endif

#define USE_SHARED_HANDLES 1

#ifdef USE_SHARED_POINTERS
#   undef USE_SHARED_POINTERS
#endif

#define USE_SHARED_POINTERS 0

// =================================================================================================
// texture data buffer handling

class TextureBuffer
{
public:

    class BufferInfo {
    public:
        int     m_width;
        int     m_height;
        int     m_componentCount;
        GLenum  m_internalFormat;
        GLenum  m_format;
        int     m_dataSize;

        BufferInfo(int width = 0, int height = 0, int componentCount = 0, int internalFormat = 0, int format = 0)
            : m_width(width)
            , m_height(height)
            , m_componentCount(componentCount)
            , m_internalFormat(internalFormat)
            , m_format(format)
            , m_dataSize(width * height * componentCount)
        {
        }

        void Reset(void)
            noexcept
        {
            m_width = 0;
            m_height = 0;
            m_componentCount = 0;
            m_internalFormat = 0;
            m_format = 0;
            m_dataSize = 0;
        }
    };

    BufferInfo   m_info;
#if USE_SHARED_POINTERS
    SharedPointer<uint8_t>  m_data;
#else
    ManagedArray<uint8_t>   m_data;
#endif
#ifdef _DEBUG
    String              m_name{ "" };
#endif
    uint32_t            m_refCount{ 0 };
    bool                m_isPosterized{ false };
    bool                m_isCartoonized{ false };

    TextureBuffer()
        : m_data()
    {  }

    ~TextureBuffer() {
#if USE_SHARED_POINTERS
        if (m_data.IsValid())
            m_data.Release();
#else
        m_data.Reset();
#endif
    }

    TextureBuffer(TextureBuffer const& other);

    TextureBuffer(TextureBuffer&& other) noexcept;

    TextureBuffer(SDL_Surface* source, bool premultiply, bool flipVertically);

    void Reset(void)
        noexcept;

    bool Allocate(int width, int height, int componentCount, void* data = nullptr) noexcept;

    TextureBuffer& Create(SDL_Surface* source, bool premultiply, bool flipVertically);

    void FlipSurface(SDL_Surface* source)
        noexcept;

    uint8_t Premultiply(uint16_t c, uint16_t a) noexcept;

    void Premultiply(void);

    TextureBuffer& operator= (const TextureBuffer& other)
        noexcept;

    TextureBuffer& operator= (TextureBuffer&& other)
        noexcept;
    // CTextureBuffer& operator= (CTextureBuffer&& other);

    TextureBuffer& Copy(TextureBuffer& other)
        noexcept;

    TextureBuffer& Move(TextureBuffer& other)
        noexcept;

    inline uint8_t* DataBuffer(void) noexcept {
        return m_data.Data();
    }

    inline int Width(void) noexcept {
        return m_info.m_width;
    }

    inline int Height(void) noexcept {
        return m_info.m_height;
    }

    inline uint32_t RefCount(void) noexcept {
        return m_refCount > 0;
    }

    void BoxBlur(uint16_t strength = 4);

    void GaussBlur(uint16_t strength = 4);

    void Posterize(uint16_t gradients = 7);

    void Cartoonize(uint16_t blurStrength = 4, uint16_t gradients = 7, uint16_t outlinePasses = 4);
};

// =================================================================================================
