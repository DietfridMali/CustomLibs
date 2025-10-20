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
#include "texturebuffer.h"

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

    void Cartoonize(uint16_t blurStrength = 4, uint16_t gradients = 15, uint16_t outlinePasses = 4);


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
