#define NOMINMAX

#include "texturearray.h"
#include "vkcontext.h"
#include "vkupload.h"
#include "gfxpixelformat_vk.h"

// =================================================================================================
// Vulkan 2D texture array implementation

bool GfxTextureArray::Create(String name, int layerWidth, int layerHeight, int layerCount, bool useMipMaps) {
    if (not CreateLayers(name, layerWidth, layerHeight, layerCount))
        return false;
    if (not Texture::Create()) {
        DestroyLayers();
        return false;
    }
    m_name = name;
    m_useMipMaps = useMipMaps;
    return true;
}

// -------------------------------------------------------------------------------------------------

void GfxTextureArray::Destroy(void) {
    Texture::Destroy();
    DestroyLayers();
}

// -------------------------------------------------------------------------------------------------
// One copy per (layer, mip). The chain is built here rather than by the driver because Vulkan has no
// glGenerateMipmap - the same reason texture_mips.h exists for 3D textures.

bool GfxTextureArray::Deploy(int /*bufferIndex*/) {
    if (m_isDeployed)
        return true;
    if (not HasLayers())
        return false;
    // The resource format below is fixed at RGBA8; anything else would need a matching one picked
    // here, and there is no caller for that yet. OpenGL derives its format from the component count
    // because its upload call takes one - these two take a format enum instead.
    if (m_components != 4)
        return false;

    const int mipCount = MipCount(m_useMipMaps != 0);

    AutoArray<uint8_t>          chains;
    AutoArray<const uint8_t*>   layerPtrs;

    if (not BuildMipChains(mipCount, chains, layerPtrs))
        return false;

    if (not CreateTextureResource(m_layerWidth, m_layerHeight, m_layerCount, mipCount, VK_FORMAT_R8G8B8A8_UNORM))
        return false;
    if (not UploadTextureArrayData(m_image, m_layoutTracker, layerPtrs.DataPtr(), m_layerCount,
                                   m_layerWidth, m_layerHeight, m_components, mipCount))
        return false;
    if (not CreateSRV())
        return false;

    SetParams();
    m_isDeployed = true;
    return true;
}

// -------------------------------------------------------------------------------------------------

bool GfxTextureArray::UpdateLayer(int layerIndex) {
    if (not m_isDeployed or (layerIndex < 0) or (layerIndex >= m_layerCount))
        return false;

    const int mipCount = MipCount(m_useMipMaps != 0);

    AutoArray<uint8_t>          chains;
    AutoArray<const uint8_t*>   layerPtrs;

    // BuildMipChains () works on the whole stack; only this layer's chain is uploaded from it. Building
    // all of them to send one up is wasteful but keeps one code path, and this runs on a texture change,
    // not per frame.
    if (not BuildMipChains(mipCount, chains, layerPtrs))
        return false;

    const uint8_t* layer = layerPtrs[layerIndex];

    return UploadTextureArrayData(m_image, m_layoutTracker, &layer, 1,
                                  m_layerWidth, m_layerHeight, m_components, mipCount, layerIndex);
}

// =================================================================================================
