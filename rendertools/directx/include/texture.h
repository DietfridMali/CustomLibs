#pragma once

#define _TEXTURE_H

#include "framework.h"
#include "std_defines.h"
#include "rendertypes.h"
#include "array.hpp"
#include "string.hpp"
#include "list.hpp"
#include "conversions.hpp"
#include "avltree.hpp"
#include "opengl_states.h"
#include "texturebuffer.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL.h"
#include "SDL_image.h"
#pragma warning(pop)

// =================================================================================================
// DX12 Texture
//
// Replaces the OGL Texture (GLuint / SharedTextureHandle) with:
//   • ComPtr<ID3D12Resource> m_resource — default-heap texture resource.
//   • uint32_t m_handle — SRV descriptor-heap index (UINT32_MAX = invalid).
//     Named m_handle for source compatibility (FBO::BufferHandle assignment, etc.).
//   • Bind(slot)  → openGLStates.BindTexture2D(m_handle, slot)
//   • Deploy()    → uploads pixel data to GPU via a temporary upload resource.
//
// SharedTextureHandle / SharedGLHandle are NOT used in DX12.

class Texture;

using TextureList  = List<Texture*>;
using TextureArray = AutoArray<Texture*>;

// =================================================================================================

struct TextureCreationParams {
    bool     premultiply{ false };
    bool     flipVertically{ false };
    bool     cartoonize{ false };
    bool     isRequired{ true };
    uint16_t blur{ 4 };
    uint16_t gradients{ 7 };
    uint16_t outline{ 4 };
};

// =================================================================================================

class AbstractTexture {
public:
    virtual bool Create(void)   = 0;
    virtual void Destroy(void)  = 0;
    virtual bool IsAvailable(void) = 0;
    virtual bool Bind(int tmuIndex) = 0;
    virtual void Release(void)  = 0;
    virtual void SetParams(bool forceUpdate = false) = 0;
    virtual bool Deploy(int bufferIndex = 0) = 0;
    virtual bool Load(String& folder, List<String>& fileNames,
                      const TextureCreationParams& params) = 0;
};

// =================================================================================================

struct RenderOffsets { float x, y; };

// =================================================================================================

class Texture : public AbstractTexture
{
public:
    // m_handle stores the SRV descriptor-heap index (UINT32_MAX = invalid).
    // Named m_handle for source-level compatibility with existing assignment sites
    // (e.g. m_renderTexture.m_handle = fbo->BufferHandle(0)).
    uint32_t                    m_handle{ UINT32_MAX };

    ComPtr<ID3D12Resource>      m_resource;   // default-heap resource (D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    String                      m_name;
    List<TextureBuffer*>        m_buffers;
    List<String>                m_filenames;
    GLenum                      m_type{ GL_TEXTURE_2D };
    int                         m_tmuIndex{ -1 };
    int                         m_wrapMode{ GL_REPEAT };
    int                         m_useMipMaps{ false };
    bool                        m_hasBuffer{ false };
    bool                        m_hasParams{ false };
    bool                        m_isValid{ false };

    static uint32_t             nullHandle;   // UINT32_MAX — matches OGL Texture::nullHandle usage

    static inline AVLTree<String, Texture*> textureLUT;

    static inline size_t CreateID(void) noexcept {
        static size_t textureID = 0;
        return ++textureID;
    }

    static int  CompareTextures(void* context, const String& k1, const String& k2);
    static void SetupLUT(void) noexcept {
        static bool needSetup = true;
        if (needSetup) { textureLUT.SetComparator(&Texture::CompareTextures); needSetup = false; }
    }
    static inline bool UpdateLUT(int update = -1) {
        static bool updateLUT = true;
        if (update != -1) updateLUT = (update == 1);
        return updateLUT;
    }

    explicit Texture(uint32_t handle = UINT32_MAX,
                     int type     = GL_TEXTURE_2D,
                     int wrapMode = GL_CLAMP_TO_EDGE);

    ~Texture() noexcept;

    inline void Register(String& name) {
        m_name = name;
        textureLUT.Insert(m_name, this);
    }

    Texture(const Texture& other)   { Copy(other); }
    Texture(Texture&& other) noexcept { Move(other); }
    Texture& operator=(const Texture& other)   { return Copy(other); }
    Texture& operator=(Texture&& other) noexcept { return Move(other); }

    Texture& Copy(const Texture& other);
    Texture& Move(Texture& other) noexcept;

    inline bool operator==(const Texture& o) const noexcept { return m_handle == o.m_handle; }
    inline bool operator!=(const Texture& o) const noexcept { return m_handle != o.m_handle; }

    virtual bool Create(void)          override;
    virtual void Destroy(void)         override;
    virtual bool IsAvailable(void)     override;
    virtual bool Bind(int tmuIndex = 0) override;
    virtual void Release(void)         override;
    virtual void SetParams(bool forceUpdate = false) override;
    virtual bool Deploy(int bufferIndex = 0)         override;
    virtual bool Load(String& folder, List<String>& fileNames,
                      const TextureCreationParams& params) override;

    inline bool Enable(int tmuIndex = 0) { return Bind(tmuIndex); }
    inline void Disable(void)            { Release(); }

    void SetWrapping(int wrapMode = -1) noexcept;

    bool Redeploy(void);

    bool CreateFromFile(String folder, List<String>& fileNames,
                        const TextureCreationParams& params);
    bool CreateFromSurface(SDL_Surface* surface, const TextureCreationParams& params);
    void Cartoonize(uint16_t blurStrength = 4, uint16_t gradients = 7, uint16_t outlinePasses = 4);

    inline size_t TextureCount(void) noexcept { return m_buffers.Length(); }
    inline int GetWidth(int i = 0)   noexcept { return (i < m_buffers.Length()) ? m_buffers[i]->m_info.m_width  : 0; }
    inline int GetHeight(int i = 0)  noexcept { return (i < m_buffers.Length()) ? m_buffers[i]->m_info.m_height : 0; }
    inline uint8_t* GetData(int i = 0)        { return (i < m_buffers.Length()) ? static_cast<uint8_t*>(m_buffers[i]->DataBuffer()) : nullptr; }
    inline int  Type(void)     noexcept { return m_type; }
    inline int  WrapMode(void) noexcept { return m_wrapMode; }
    inline String GetName(void)        { return m_name; }

    inline TextureType GetTextureType(void) const noexcept {
        if (m_type == GL_TEXTURE_CUBE_MAP) return TextureType::CubeMap;
        return TextureType::Texture2D;
    }

    // Static release helpers — clear the slot in openGLStates.
    template<GLenum typeID>
    static inline void Release(int tmuIndex) noexcept {
        if (tmuIndex >= 0)
            openGLStates.BindTexture(typeID, UINT32_MAX, tmuIndex);
        openGLStates.ActiveTexture(GL_TEXTURE0);
    }

    inline bool& HasBuffer(void) noexcept { return m_hasBuffer; }

    static RenderOffsets ComputeOffsets(int w, int h,
                                        int viewportWidth, int viewportHeight,
                                        int renderAreaWidth, int renderAreaHeight) noexcept;
};

// =================================================================================================

class TiledTexture : public Texture
{
public:
    TiledTexture() : Texture(UINT32_MAX, GL_TEXTURE_2D, GL_REPEAT) {}
    ~TiledTexture() = default;
    virtual void SetParams(bool forceUpdate = false) override;
};

// =================================================================================================

class FBOTexture : public Texture
{
public:
    FBOTexture() = default;
    ~FBOTexture() = default;
    virtual void SetParams(bool forceUpdate = false) override;
};

// =================================================================================================

class ShadowTexture : public Texture
{
public:
    ShadowTexture() = default;
    ~ShadowTexture() = default;
    virtual void SetParams(bool forceUpdate = false) override;
};

// =================================================================================================

#include "lineartexture.h"

// =================================================================================================
