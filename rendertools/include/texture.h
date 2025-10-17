#pragma once

#define _TEXTURE_H

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

#define USE_SHARED_POINTERS 0

// =================================================================================================
// texture handling classes

class AbstractTexture {
public:
    virtual bool Create(void) = 0;

    virtual void Destroy(void) = 0;

    virtual bool IsAvailable(void) = 0;

    virtual bool Bind(int tmuIndex) = 0;

    virtual void Release(int tmuIndex) = 0;

    virtual void SetParams(bool enforce = false) = 0;

    virtual void Deploy(int bufferIndex = 0) = 0;

    virtual bool Enable(int tmuIndex = 0) = 0;

    virtual void Disable(int tmuIndex = 0) = 0;

    virtual bool Load(List<String>& fileNames, bool premultiply = false, bool flipVertically = false) = 0;
};

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


class Texture 
    : public AbstractTexture
{
public:
#if USE_SHARED_HANDLES
    SharedTextureHandle         m_handle;
#else
    GLuint                      m_handle;
#endif
    size_t                      m_id{ 0 };
    String                      m_name{ "" };
    List<TextureBuffer*>        m_buffers;
    List<String>                m_filenames;
    GLenum                      m_type{ GL_TEXTURE_2D };
    int                         m_tmuIndex{ -1 };
    int                         m_wrapMode{ GL_REPEAT };
    int                         m_useMipMaps{ false };
    bool                        m_hasBuffer{ false };
    bool                        m_hasParams{ false };
    bool                        m_isValid{ false };

    static SharedTextureHandle  nullHandle;

    static inline AVLTree<size_t, Texture*>    textureLUT;

    static inline size_t GetID(void) noexcept {
        static size_t textureID = 0;
        return ++textureID;
    }

    static int CompareTextures(void* context, const size_t& key1, const size_t& key2);

    static void SetupLUT(void) noexcept {
        static bool needSetup = true;
        if (needSetup) {
            textureLUT.SetComparator(&Texture::CompareTextures);
            needSetup = false;
        }
    }

    static inline bool UpdateLUT(int update = -1) { // -1: just query; 0: false; 1: true
        static bool updateLUT = true;
        if (update != -1)
            updateLUT = update == 1;
        return updateLUT;
    }

    Texture(GLuint handle = 0, int type = GL_TEXTURE_2D, int wrapMode = GL_CLAMP_TO_EDGE);

    ~Texture();

    inline void Register(void) {
        m_id = GetID();
        textureLUT.Insert(m_id, this);
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

    virtual bool Create(void) override;

    virtual void Destroy(void) override;

    virtual bool IsAvailable(void) override;

    virtual bool Bind(int tmuIndex = 0) override;

    virtual void Release(int tmuIndex = 0) override;

    virtual void SetParams(bool enforce = false) override;

    void SetWrapping(int wrapMode = -1)
        noexcept;

    virtual bool Enable(int tmuIndex = 0) override;

    virtual void Disable(int tmuIndex = 0) override;

    virtual void Deploy(int bufferIndex = 0) override;

    virtual bool Load(List<String>& fileNames, bool premultiply = false, bool flipVertically = false) override;

    bool CreateFromFile(List<String>& fileNames, bool premultiply = false, bool flipVertically = false);

    bool CreateFromSurface(SDL_Surface* surface, bool premultiply = false, bool flipVertically = false);

    inline void SetID(uint32_t id) noexcept {
        m_id = id;
    }

    inline size_t TextureCount(void)
        noexcept
    {
        return m_buffers.Length();
    }

    inline int GetWidth(int i = 0)
        noexcept
    {
        return (i < m_buffers.Length()) ? m_buffers[i]->m_info.m_width : 0;
    }

    inline int GetHeight(int i = 0)
        noexcept
    {
        return (i < m_buffers.Length()) ? m_buffers[i]->m_info.m_height : 0;
    }

    inline uint8_t* GetData(int i = 0) {
        return (i < m_buffers.Length()) ? static_cast<uint8_t*>(m_buffers[i]->DataBuffer()) : nullptr;
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

    template <GLenum typeID>
    inline static void Release(int tmuIndex)
        noexcept
    {
        if (tmuIndex >= 0)
            openGLStates.BindTexture<typeID>(0, tmuIndex);
        openGLStates.ActiveTexture(GL_TEXTURE0); // always reset!
    }

    inline bool& HasBuffer(void)
        noexcept
    {
        return m_hasBuffer;
    }

    static tRenderOffsets ComputeOffsets(int w, int h, int viewportWidth, int viewportHeight, int renderAreaWidth, int renderAreaHeight)
        noexcept;
};

// =================================================================================================

class TiledTexture
    : public Texture
{
public:
    TiledTexture()
        : Texture(0, GL_TEXTURE_2D, GL_REPEAT) 
    { }

    ~TiledTexture() = default;

    virtual void SetParams(bool enforce = false) override;
};

// =================================================================================================

class FBOTexture
    : public Texture
{
public:
    FBOTexture() = default;

    ~FBOTexture() = default;

    virtual void SetParams(bool enforce = false) override;
};

// =================================================================================================

#include "lineartexture.h"

// =================================================================================================
