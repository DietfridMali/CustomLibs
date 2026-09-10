#define NOMINMAX

#include "texturearray.h"
#include "descriptor_heap.h"
#include "dx12context.h"
#include "dx12upload.h"
#include "gfxpixelformat_dx.h"

// =================================================================================================
// DX12 2D texture array implementation

// THE HANDLE FIRST, THE SLOTS AFTER IT. Texture::Create () begins with Destroy (), and Destroy () is
// virtual: it lands in GfxTextureArray::Destroy (), which calls DestroySlots (). Creating the slots
// first therefore threw them away again one line later - and Create () still returned true, because
// the handle was there. The array then had m_slotCount 0, every SetSlot () failed on HasSlots (),
// and whoever filled it discarded the whole array.
bool GfxTextureArray::Create(String name, int slotWidth, int slotHeight, int slotCount, bool useMipMaps) {
    if (not Texture::Create())          // allocates the SRV descriptor index and sets m_isValid
        return false;
    if (not CreateSlots(name, slotWidth, slotHeight, slotCount)) {
        Texture::Destroy();
        return false;
    }
    m_name = name;
    m_useMipMaps = useMipMaps;
    return true;
}

// -------------------------------------------------------------------------------------------------

bool GfxTextureArray::CreateCompressed(String name, int slotWidth, int slotHeight, int slotCount, GfxPixelFormat format, int mipCount) {
    if (not Texture::Create())
        return false;
    if (not CreateCompressedSlots(name, slotWidth, slotHeight, slotCount, format, mipCount)) {
        Texture::Destroy();
        return false;
    }
    m_name = name;
    m_useMipMaps = (mipCount > 1);
    m_compression = GfxFormatToCompression(format);
    return true;
}

// -------------------------------------------------------------------------------------------------

bool GfxTextureArray::SetSlot(int slotIndex, TextureBuffer& buffer) {
    if (IsCompressed())
        return SetCompressedSlot(slotIndex, buffer.DataBuffer(), size_t(buffer.m_info.m_dataSize),
                                 buffer.m_info.m_width, buffer.m_info.m_height, buffer.m_info.m_gfxFormat, buffer.m_info.m_mipCount);
    return SetSlot(slotIndex, buffer.DataBuffer(), buffer.m_info.m_width, buffer.m_info.m_height, buffer.m_info.m_componentCount);
}

// -------------------------------------------------------------------------------------------------

void GfxTextureArray::Destroy(void) {
    Texture::Destroy();
    DestroySlots();
}

// -------------------------------------------------------------------------------------------------
// One subresource per (slot, mip). The chain is built here rather than by the driver because DX12
// has no glGenerateMipmap - the same reason texture_mips.h exists for 3D textures.

bool GfxTextureArray::Deploy(int /*bufferIndex*/) {
    if (m_isDeployed)
        return true;
    if (not HasSlots())
        return false;

    if (IsCompressed()) {
        const GfxPixelFormat fmt = GfxLinearFormat(m_format);
        AutoArray<const uint8_t*> slotPtrs;
        if (not SlotPointers(slotPtrs))
            return false;
        if (not CreateTextureResource(m_slotWidth, m_slotHeight, m_slotCount, m_mipCount, ToDXGIFormat(fmt)))
            return false;
        if (not UploadCompressedData(dx12Context.Device(), m_resource.Get(), slotPtrs.DataPtr(),
                                     m_slotCount, m_slotWidth, m_slotHeight, fmt, m_mipCount))
            return false;
        if (not CreateSRV())
            return false;
        SetParams();
        m_isDeployed = true;
        return true;
    }

    // The resource format below is fixed at RGBA8; anything else would need a matching one picked
    // here, and there is no caller for that yet. OpenGL derives its format from the component count
    // because its upload call takes one - these two take a format enum instead.
    if (m_components != 4)
        return false;

    const int mipCount = MipCount(m_useMipMaps != 0);

    AutoArray<uint8_t>          chains;
    AutoArray<const uint8_t*>   slotPtrs;

    if (not BuildMipChains(mipCount, chains, slotPtrs))
        return false;

    if (not CreateTextureResource(m_slotWidth, m_slotHeight, m_slotCount, mipCount, DXGI_FORMAT_R8G8B8A8_UNORM))
        return false;
    if (not UploadTextureArrayData(dx12Context.Device(), m_resource.Get(), slotPtrs.DataPtr(),
                                   m_slotCount, m_slotWidth, m_slotHeight, m_components, mipCount))
        return false;
    if (not CreateSRV())
        return false;

    SetParams();
    m_isDeployed = true;
    return true;
}

// -------------------------------------------------------------------------------------------------
// There is no per slot upload short of rebuilding that slot's chain, and the whole array's resource
// stays as it is - only this slot's subresources are written again.

bool GfxTextureArray::UpdateSlot(int slotIndex) {
    if (not m_isDeployed or (slotIndex < 0) or (slotIndex >= m_slotCount))
        return false;

    if (IsCompressed()) {
        const uint8_t* slot = SlotData(slotIndex);
        return UploadCompressedData(dx12Context.Device(), m_resource.Get(), &slot, 1,
                                    m_slotWidth, m_slotHeight, GfxLinearFormat(m_format), m_mipCount, slotIndex, true);
    }

    const int mipCount = MipCount(m_useMipMaps != 0);

    AutoArray<uint8_t>          chains;
    AutoArray<const uint8_t*>   slotPtrs;

    // BuildMipChains () works on the whole stack; only this slot's chain is uploaded from it. Building
    // all of them to send one up is wasteful but keeps one code path, and this runs on a texture change,
    // not per frame.
    if (not BuildMipChains(mipCount, chains, slotPtrs))
        return false;

    const uint8_t* slot = slotPtrs[slotIndex];

    // isRefresh: the resource left Deploy () in PIXEL_SHADER_RESOURCE and has to be taken back to
    // COPY_DEST before it can be written again.
    return UploadTextureArrayData(dx12Context.Device(), m_resource.Get(), &slot, 1,
                                  m_slotWidth, m_slotHeight, m_components, mipCount, slotIndex, true);
}

// =================================================================================================
