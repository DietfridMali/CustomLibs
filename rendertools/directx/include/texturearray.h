#pragma once

#include "texture.h"
#include "base_texturearray.h"

// =================================================================================================
// A Texture2DArray. Everything about layers and staging is in BaseTextureArray; what is added here is
// the upload, exactly as Cubemap adds only its six face uploads on top of Texture.
//
// Not to be confused with TextureArray in texture.h, which is AutoArray<Texture*> - a list of separate
// textures, the opposite of what this is.
//
// The mip chain is built on the CPU (BaseTextureArray::BuildMipChains) and uploaded level by level.
// That is the same split texture_mips.h already makes for 3D textures: DX12 and Vulkan have no
// glGenerateMipmap, so they carry the pyramid up themselves while OpenGL lets the driver build it.
// The result is the same pyramid either way.

class GfxTextureArray
    : public Texture
    , public BaseTextureArray
{
public:
    GfxTextureArray()
        : Texture(UINT32_MAX, TextureType::Texture2DArray, GfxWrapMode::ClampToEdge)
    {}

    // The overload below would otherwise hide Texture's parameterless Create ().
    using Texture::Create;

    // Sprite sheets must not wrap: a bilinear tap at u = 1 would read column 0 back in. Filtering and
    // mip mapping come from Texture::SetParams (), which reads m_wrapMode and m_useMipMaps - unlike an
    // atlas an array can have a mip chain, because a mip level never mixes two layers.
    bool Create(String name, int layerWidth, int layerHeight, int layerCount, bool useMipMaps = true);

    // bufferIndex is ignored: the array has no m_buffers, its pixels come from SetLayer ().
    virtual bool Deploy(int bufferIndex = 0) override;

    // Sends one layer up again after it changed. Only valid once Deploy () has run.
    bool UpdateLayer(int layerIndex);

    virtual void Destroy(void) override;
};

// =================================================================================================
