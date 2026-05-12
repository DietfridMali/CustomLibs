#pragma once

#define _TEXTURE_H

#include "std_defines.h"
#include "rendertypes.h"
#include "glew.h"
#include "array.hpp"
#include "string.hpp"
#include "list.hpp"
#include "conversions.hpp"
#include "sharedpointer.hpp"
#include "sharedgfxhandle.hpp"
#include "avltree.hpp"
#include "gfxstates.h"
#include "texturebuffer.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL.h"
#include "SDL_image.h"
#pragma warning(pop)

class Texture;

using TextureList = List<Texture*>;

using TextureArray = AutoArray<Texture*>;

#ifdef USE_SHARED_HANDLES
#   undef USE_SHARED_HANDLES
#endif

#define USE_SHARED_HANDLES 1

#ifdef USE_SHARED_POINTERS
#   undef USE_SHARED_POINTERS
#endif

#define USE_SHARED_POINTERS 0

// =================================================================================================

struct TextureCreationParams {
    bool        premultiply{ false };
    bool        flipVertically{ false };
    bool        cartoonize{ false };
	bool        isRequired{ true };
    bool        isDisposable{ false };
    uint16_t    blur{ 4 };
    uint16_t    gradients{ 7 };
    uint16_t    outline{ 4 };
	String	    keyDecoration{ "" };
};

// =================================================================================================
// texture handling classes

class AbstractTexture {
public:
    virtual bool Create(void) = 0;

    virtual void Destroy(void) = 0;

    virtual bool IsAvailable(bool isDeploying = false) = 0;

    virtual bool Bind(int tmuIndex, bool isDeploying = false) = 0;

    virtual void Release(void) = 0;

    virtual void SetParams(bool forceUpdate = false) = 0;

    virtual bool Deploy(int bufferIndex = 0) = 0;

    virtual bool Load(String& folder, List<String>& fileNames, const TextureCreationParams& params) = 0;
};

// =================================================================================================
// texture handling: Loading from file, parameterization and sending to OpenGL driver, 
// enabling for rendering
// Base class for higher level textures (e.g. cubemaps)

struct RenderOffsets {
    float x, y;
};


class Texture 
    : public AbstractTexture
{
public:
#if USE_SHARED_HANDLES
    SharedTextureHandle         m_handle;
#else
    GLuint                      m_handle;
#endif
    String                      m_name;
    List<TextureBuffer*>        m_buffers;
    List<String>                m_filenames;
    GLenum                      m_type{ GL_TEXTURE_2D };
    int                         m_tmuIndex{ -1 };
    int                         m_wrapMode{ GL_REPEAT };
    int                         m_useMipMaps{ false };
    bool                        m_hasParams{ false };
    bool                        m_isDeployed{ false };
    bool                        m_isValid{ false };
    bool                        m_isRenderTarget{ false };
    bool                        m_isDisposable{ false };

    static SharedTextureHandle  nullHandle;

    static inline AVLTree<String, Texture*>  textureLUT;

    static inline size_t CreateID(void) noexcept {
        static size_t textureID = 0;
        return ++textureID;
    }

    static int CompareTextures(void* context, const String& key1, const String& key2);

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

    ~Texture() noexcept;

    inline void Register(String& name) {
       m_name = name;
        textureLUT.Insert(m_name, this);
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

    virtual bool IsAvailable(bool isDeploying = false) override;

    virtual bool Bind(int tmuIndex = 0, bool isDeploying = false) override;

    virtual void Release(void) override;

    inline bool IsRenderTarget(void) noexcept {
        return m_isRenderTarget;
    }

    inline bool IsDeployed(void) noexcept {
        return m_isDeployed;
    }

    inline bool Activate(int tmuIndex = 0) {
        return Bind(tmuIndex);
    }

    inline void Deactivate(void) {
        Release();
    }

    inline void Validate(void) noexcept {
        m_isValid = true;
        m_isDeployed = true;
    }

    inline void Invalidate(void) noexcept {
        m_isValid = false;
        m_isDeployed = false;
    }

    virtual void SetParams(bool forceUpdate = false) override;

    void SetWrapping(GfxWrapMode wrapMode)
        noexcept;

    virtual bool Deploy(int bufferIndex = 0) override;

    bool Redeploy(void);

    virtual bool Load(String& folder, List<String>& fileNames, const TextureCreationParams& params) override;

    bool CreateFromFile(String folder, List<String>& fileNames, const TextureCreationParams& params);

    bool CreateFromSurface(SDL_Surface* surface, const TextureCreationParams& params);

    void Cartoonize(uint16_t blurStrength = 4, uint16_t gradients = 7, uint16_t outlinePasses = 4);

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

    inline TextureType GetTextureType(void) const noexcept {
        if (m_type == GL_TEXTURE_3D)    return TextureType::Texture3D;
        if (m_type == GL_TEXTURE_CUBE_MAP) return TextureType::CubeMap;
        return TextureType::Texture2D;
    }

    inline int WrapMode(void)
        noexcept
    {
        return m_wrapMode;
    }

    inline String GetName(void) {
        return m_name;
    }

    template <GLenum typeID>
    inline static void Release(int tmuIndex)
        noexcept
    {
        if (tmuIndex >= 0)
            gfxStates.BindTexture<typeID>(0, tmuIndex);
        gfxStates.ActiveTexture(GL_TEXTURE0); // always reset!
    }

    static RenderOffsets ComputeOffsets(int w, int h, int viewportWidth, int viewportHeight, int renderAreaWidth, int renderAreaHeight)
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

    virtual void SetParams(bool forceUpdate = false) override;
};

// =================================================================================================

class RenderTargetTexture
    : public Texture
{
public:
    RenderTargetTexture() {
        m_isRenderTarget = true;
    }

    ~RenderTargetTexture();

    virtual void SetParams(bool forceUpdate = false) override;

    // The GL texture name is owned by the wrapped RenderTarget's BufferInfo. Drop our
    // borrowed handle here so the implicit ~Texture path's Texture::Destroy() sees a
    // zero handle and glDeleteTextures(0) is silently ignored per spec. Functionally
    // redundant in OpenGL (glDeleteTextures tolerates already-deleted names), but the
    // pattern is kept symmetric to the Vulkan / DX12 paths.
    virtual void Destroy(void) override;
};

// =================================================================================================

class ShadowTexture
    : public Texture
{
public:
    ShadowTexture() = default;

    ~ShadowTexture();

    virtual void SetParams(bool forceUpdate = false) override;

    // Same lifetime semantics as RenderTargetTexture.
    virtual void Destroy(void) override;
};

// =================================================================================================

#include "lineartexture.h"

// =================================================================================================
