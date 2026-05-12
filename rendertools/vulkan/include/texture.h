#pragma once

#define _TEXTURE_H

#include "vkframework.h"
#include "image_layout_tracker.h"
#include "std_defines.h"
#include "rendertypes.h"
#include "texturesampling.h"
#include "array.hpp"
#include "string.hpp"
#include "list.hpp"
#include "conversions.hpp"
#include "avltree.hpp"
#include "gfxstates.h"
#include "texturebuffer.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL.h"
#include "SDL_image.h"
#pragma warning(pop)

// =================================================================================================
// Vulkan Texture
//
// Replaces the DX12 Texture (ComPtr<ID3D12Resource> + uint32_t SRV index) with:
//   • VkImage m_image          — the texture image, allocated via VMA.
//   • VkImageView m_imageView  — image view used as the t-slot binding source (combined image
//     sampler descriptor). Vulkan's analogue to the DX12 SRV.
//   • VmaAllocation m_allocation — VMA backing allocation, freed in Destroy.
//   • ImageLayoutTracker m_layoutTracker — current VkImageLayout / stage / access of the image.
//
// m_handle (uint32_t) is kept for source-level compatibility with existing assignment sites
// (e.g. m_renderTexture.m_handle = renderTarget->BufferHandle(0)). It is a logical id only —
// no GPU descriptor-heap index in Vulkan; the Bind path writes m_imageView + m_sampling-derived
// VkSampler into the per-frame descriptor-set bind table (Phase C).

class Texture;

using TextureList  = List<Texture*>;
using TextureArray = AutoArray<Texture*>;

// =================================================================================================

struct TextureCreationParams {
    bool     premultiply{ false };
    bool     flipVertically{ false };
    bool     cartoonize{ false };
    bool     isRequired{ true };
    bool     isDisposable{ false };
    uint16_t blur{ 4 };
    uint16_t gradients{ 7 };
    uint16_t outline{ 4 };
    String   keyDecoration{ "{}" };
};

// =================================================================================================

class AbstractTexture {
public:
    virtual bool Create(void) = 0;
    virtual void Destroy(void) = 0;
    virtual bool IsAvailable(void) = 0;
    virtual bool Bind(int tmuIndex) = 0;
    virtual void Release(void) = 0;
    virtual void SetParams(bool forceUpdate = false) = 0;
    virtual bool Deploy(int bufferIndex = 0) = 0;
    virtual bool Load(String& folder, List<String>& fileNames, const TextureCreationParams& params) = 0;
};

// =================================================================================================

struct RenderOffsets { float x, y; };

// =================================================================================================

class Texture
    : public AbstractTexture
{
public:
    // m_handle is a logical id only (no GPU-side meaning in Vulkan). Kept for source
    // compatibility with sites that assign from RenderTarget::BufferHandle etc.
    uint32_t                    m_handle{ UINT32_MAX };

    VkImage                     m_image{ VK_NULL_HANDLE };
    VkImageView                 m_imageView{ VK_NULL_HANDLE };
    VmaAllocation               m_allocation{ VK_NULL_HANDLE };
    ImageLayoutTracker          m_layoutTracker;

    String                      m_name;
    List<TextureBuffer*>        m_buffers;
    List<String>                m_filenames;
    TextureType                 m_type{ TextureType::Texture2D };
    int                         m_tmuIndex{ -1 };
    GfxWrapMode                 m_wrapMode{ GfxWrapMode::Repeat };
    int                         m_useMipMaps{ false };
    bool                        m_hasParams{ false };
    bool                        m_isValid{ false };
    bool                        m_isDeployed{ false };
    bool                        m_isRenderTarget{ false };
    bool                        m_isDisposable{ false };
    TextureSampling             m_sampling;

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

    explicit Texture(uint32_t handle = UINT32_MAX, TextureType type = TextureType::Texture2D, GfxWrapMode wrap = GfxWrapMode::ClampToEdge);

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

    Texture& Move(Texture& other) noexcept;

    virtual bool Create(void) override;

    virtual void Destroy(void) override;

    virtual bool IsAvailable(void) override;

    virtual bool Bind(int tmuIndex = 0) override;

    virtual void Release(void) override;

    virtual void SetParams(bool forceUpdate = false) override;

    virtual bool Deploy(int bufferIndex = 0) override;

    virtual bool Load(String& folder, List<String>& fileNames, const TextureCreationParams& params) override;

    inline bool Activate(int tmuIndex = 0) {
        return Bind(tmuIndex);
    }

    inline void Deactivate(void) {
        Release();
    }

    inline void Validate(bool isValid) noexcept {
        m_isValid = isValid;
    }

    void SetWrapping(GfxWrapMode wrapMode) noexcept;

    bool Redeploy(void);

    bool CreateFromFile(String folder, List<String>& fileNames, const TextureCreationParams& params);

    bool CreateFromSurface(SDL_Surface* surface, const TextureCreationParams& params);

    void Cartoonize(uint16_t blurStrength = 4, uint16_t gradients = 7, uint16_t outlinePasses = 4);

    inline size_t TextureCount(void) noexcept {
        return m_buffers.Length();
    }

    inline int GetWidth(int i = 0) noexcept {
        return (i < m_buffers.Length()) ? m_buffers[i]->m_info.m_width  : 0;
    }

    inline int GetHeight(int i = 0) noexcept {
        return (i < m_buffers.Length()) ? m_buffers[i]->m_info.m_height : 0;
    }

    inline uint8_t* GetData(int i = 0) {
        return (i < m_buffers.Length()) ? static_cast<uint8_t*>(m_buffers[i]->DataBuffer()) : nullptr;
    }

    inline TextureType Type(void) noexcept {
        return m_type;
    }

    inline GfxWrapMode  WrapMode(void) noexcept {
        return m_wrapMode;
    }

    inline String GetName(void) {
        return m_name;
    }

    inline TextureType GetTextureType(void) const noexcept {
        return m_type;
    }

    // Static release helpers — clear the slot in gfxStates.
    template<TextureType typeID>
    static inline void Release(int tmuIndex) noexcept {
        if (tmuIndex >= 0)
            gfxStates.BindTexture(TextureTypeToGLenum(typeID), UINT32_MAX, tmuIndex);
    }

    inline bool IsRenderTarget(void) noexcept {
        return m_isRenderTarget;
    }

    inline bool IsDeployed(void) const noexcept {
        return m_isDeployed;
    }

    inline void Validate(void) noexcept {
        m_isValid = true;
        m_isDeployed = true;
    }

    inline void Invalidate(void) noexcept {
        m_isValid = false;
        m_isDeployed = false;
    }

    bool CreateTextureResource(int w, int h, int arraySize);

    bool CreateSRV(void);

    static RenderOffsets ComputeOffsets(int w, int h, int viewportWidth, int viewportHeight, int renderAreaWidth, int renderAreaHeight) noexcept;
};

// =================================================================================================

class TiledTexture
    : public Texture
{
public:
    TiledTexture()
        : Texture(UINT32_MAX, TextureType::Texture2D, GfxWrapMode::Repeat)
    {}
    ~TiledTexture() = default;

    virtual void SetParams(bool forceUpdate = false) override;
};

// =================================================================================================

class RenderTargetTexture
    : public Texture
{
public:
    RenderTargetTexture() = default;
    ~RenderTargetTexture();
    virtual void SetParams(bool forceUpdate = false) override;

    // The image view + image are owned by the wrapped RenderTarget's BufferInfo,
    // not by the texture wrapper. Destroy must NOT call vkDestroyImageView /
    // vmaDestroyImage on them — that would yank them from under the RT.
    virtual void Destroy(void) override;
};

// =================================================================================================

class ShadowTexture : public Texture
{
public:
    ShadowTexture() = default;
    ~ShadowTexture();
    virtual void SetParams(bool forceUpdate = false) override;

    // Same lifetime semantics as RenderTargetTexture: the shadow image view is owned by
    // the source RenderTarget (depth / sample view), not by this wrapper.
    virtual void Destroy(void) override;
};

// =================================================================================================

#include "lineartexture.h"

// =================================================================================================
