#define NOMINMAX

#include "texturearray.h"
#include "descriptor_heap.h"
#include "dx12context.h"
#include "dx12upload.h"
#include "gfxpixelformat_dx.h"

// =================================================================================================
// DX12 2D texture array implementation

bool GfxTextureArray::Create(String name, int layerWidth, int layerHeight, int layerCount, bool useMipMaps) {
    if (not CreateLayers(name, layerWidth, layerHeight, layerCount))
        return false;
    if (not Texture::Create()) {        // allocates the SRV descriptor index and sets m_isValid
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
// One subresource per (layer, mip). The chain is built here rather than by the driver because DX12
// has no glGenerateMipmap - the same reason texture_mips.h exists for 3D textures.

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

    if (not CreateTextureResource(m_layerWidth, m_layerHeight, m_layerCount, mipCount, DXGI_FORMAT_R8G8B8A8_UNORM))
        return false;
    if (not UploadTextureArrayData(dx12Context.Device(), m_resource.Get(), layerPtrs.DataPtr(),
                                   m_layerCount, m_layerWidth, m_layerHeight, m_components, mipCount))
        return false;
    if (not CreateSRV())
        return false;

    SetParams();
    m_isDeployed = true;
    return true;
}

// -------------------------------------------------------------------------------------------------
// There is no per layer upload short of rebuilding that layer's chain, and the whole array's resource
// stays as it is - only this layer's subresources are written again.

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

    return UploadTextureArrayData(dx12Context.Device(), m_resource.Get(), &layer, 1,
                                  m_layerWidth, m_layerHeight, m_components, mipCount, layerIndex);
}

// =================================================================================================
