#pragma once
#include "std_defines.h"

#include "glew.h"
#include "array.hpp"
#include "string.hpp"
#include "list.hpp"
#include "conversions.hpp"
#include "sharedpointer.hpp"
#include "sharedglhandle.hpp"

#include "SDL.h"
#include "SDL_image.h"

#define SURFACE_COLOR 0
#define OUTLINE_COLOR 1

class Texture;

using TextureList = List<Texture*>;

#ifdef USE_SHARED_HANDLES
#   undef USE_SHARED_HANDLES
#endif

#define USE_SHARED_HANDLES 1

#ifdef USE_SHARED_POINTERS
#   undef USE_SHARED_POINTERS
#endif

#define USE_SHARED_POINTERS 1

// =================================================================================================
// texture handling classes

class AbstractTexture {
public:
    virtual bool Create(void) = 0;

    virtual void Destroy(void) = 0;

    virtual bool IsAvailable(void) = 0;

    virtual void Bind(void) = 0;

    virtual void Release(void) = 0;

    virtual void SetParams(void) = 0;

    virtual void Deploy(int bufferIndex = 0) = 0;

    virtual void Enable(int tmu = 0) = 0;

    virtual void Disable(void) = 0;

    virtual bool Load(List<String>& fileNames, bool flipVertically = false) = 0;
};

// =================================================================================================
// texture data buffer handling

class TextureBuffer
{
public:

    class TextureBufferInfo {
    public:
        int                 width;
        int                 height;
        int                 componentCount;
        GLenum              internalFormat;
        GLenum              format;
        int                 dataSize;

        TextureBufferInfo()
            : width(0), height(0), componentCount(0), internalFormat(0), format(0), dataSize(0)
        {
        }

        void Reset(void)
            noexcept
        {
            width = 0;
            height = 0;
            componentCount = 0;
            internalFormat = 0;
            format = 0;
            dataSize = 0;
        }
    };

    TextureBufferInfo   m_info;
#if USE_SHARED_POINTERS
    SharedPointer<char> m_data;
#else
    char* m_data;
#endif
#ifdef _DEBUG
    String              m_name;
#endif
    //int     m_isAlias;

    TextureBuffer()
        : m_data()//, m_isAlias (false)
    {
    }

    ~TextureBuffer() {
#if USE_SHARED_POINTERS
        if (m_data.IsValid())
            m_data.Release();
#else
        if (m_data) {
            delete[](m_data);
            m_data = nullptr;
        }
#endif
    }

    TextureBuffer(TextureBuffer const& other);

    TextureBuffer(TextureBuffer&& other) noexcept;

    TextureBuffer(SDL_Surface* source, bool flipVertically);

    void Reset(void)
        noexcept;

    TextureBuffer& Create(SDL_Surface* source, bool flipVertically);

    void FlipSurface(SDL_Surface* source)
        noexcept;

    TextureBuffer& operator= (const TextureBuffer& other)
        noexcept;

    TextureBuffer& operator= (TextureBuffer&& other)
        noexcept;
    // CTextureBuffer& operator= (CTextureBuffer&& other);

    TextureBuffer& Copy(TextureBuffer& other)
        noexcept;

    TextureBuffer& Move(TextureBuffer& other)
        noexcept;

    // CTextureBuffer(CTextureBuffer&& other) = default;            // move construct

    // CTextureBuffer& operator=(CTextureBuffer and other) = default; // move assignment
};

// =================================================================================================
// texture handling: Loading from file, parameterization and sending to OpenGL driver, 
// enabling for rendering
// Base class for higher level textures (e.g. cubemaps)

typedef struct {
    float x, y;
} tRenderOffsets;


class Texture : public AbstractTexture
{
public:
#if USE_SHARED_HANDLES
    SharedTextureHandle     m_handle;
#else
    GLuint                  m_handle;
#endif
    String                  m_name;
    List<TextureBuffer*>    m_buffers;
    List<String>            m_filenames;
    int                     m_type;
    int                     m_wrapMode;
    int                     m_useMipMaps;
    bool                    m_hasBuffer;
    bool                    m_isValid;

    static SharedTextureHandle nullHandle;

    Texture(GLuint handle = 0, int type = GL_TEXTURE_2D, int wrapMode = GL_CLAMP_TO_EDGE)
        : m_handle(handle), m_type(type), m_wrapMode(wrapMode), m_useMipMaps(false), m_isValid(true), m_hasBuffer(false)
    {
    }

    ~Texture()
        noexcept
    {
        if (m_isValid) {
            Destroy();
            m_isValid = false;
        }
        else
            fprintf(stderr, "repeatedly destroying texture '%s'\n", (char*)m_filenames[0]);
    }

    Texture(const Texture& other) {
        Copy(other);
    }


    Texture(Texture&& other) noexcept {
        Move(other);
    }


    Texture& operator=(const Texture& other) {
        return Copy(other);
    }


    Texture& operator=(Texture&& other) noexcept {
        return Move(other);
    }


    Texture& Copy(const Texture& other);

    Texture& Move(Texture& other)
        noexcept;

    inline bool operator== (Texture const& other) const
        noexcept
    {
        return m_handle == other.m_handle;
    }

    inline bool operator!= (Texture const& other) const
        noexcept
    {
        return m_handle != other.m_handle;
    }

    virtual bool Create(void);

    virtual void Destroy(void);

    virtual bool IsAvailable(void);

    virtual void Bind(void);

    virtual void Release(void);

    virtual void SetParams(void);

    void Wrap(void)
        noexcept;

    virtual void Enable(int tmu = 0);

    virtual void Disable(void);

    virtual void Deploy(int bufferIndex = 0);

    virtual bool Load(List<String>& fileNames, bool flipVertically);

    bool CreateFromFile(List<String>& fileNames, bool flipVertically = false);

    bool CreateFromSurface(SDL_Surface* surface, bool flipVertically = false);

    inline size_t TextureCount(void)
        noexcept
    {
        return m_buffers.Length();
    }

    inline int GetWidth(int i = 0)
        noexcept
    {
        return m_buffers[i]->m_info.width;
    }

    inline int GetHeight(int i = 0)
        noexcept
    {
        return m_buffers[i]->m_info.height;
    }

    inline int Type(void)
        noexcept
    {
        return m_type;
    }

    inline int WrapMode(void)
        noexcept
    {
        return m_wrapMode;
    }

    inline void SetName(String name) {
        m_name = name;
    }

    inline String GetName(void) {
        return m_name;
    }

    inline bool& HasBuffer(void)
        noexcept
    {
        return m_hasBuffer;
    }

    inline static void Release(int tmuIndex)
        noexcept
    {
        glActiveTexture(GL_TEXTURE0 + tmuIndex);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0); // always reset!
    }

    static tRenderOffsets ComputeOffsets(int w, int h, int viewportWidth, int viewportHeight, int renderAreaWidth, int renderAreaHeight)
        noexcept;
};

// =================================================================================================

class TiledTexture
    : public Texture
{
public:
    TiledTexture() = default;
    ~TiledTexture() = default;

    void SetParams(void);
};

// =================================================================================================
