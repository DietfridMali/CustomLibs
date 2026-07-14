#define NOMINMAX

#include "cubemap.h"
#include "texturebuffer.h"
#include "descriptor_heap.h"
#include "dx12context.h"
#include "dx12upload.h"
#include "gfxpixelformat_dx.h"

// =================================================================================================
// DX12 Cubemap implementation

void Cubemap::SetParams(void) {
    m_hasParams = true;
    m_sampling.minFilter     = GfxFilterMode::Linear;
    m_sampling.magFilter     = GfxFilterMode::Linear;
    m_sampling.mipMode       = GfxMipMode::None;
    m_sampling.wrapU         = GfxWrapMode::ClampToEdge;
    m_sampling.wrapV         = GfxWrapMode::ClampToEdge;
    m_sampling.wrapW         = GfxWrapMode::ClampToEdge;
    m_sampling.compareFunc   = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


bool Cubemap::Deploy(int /*bufferIndex*/)
{
    if (m_isDeployed)
        return true;
    if (m_buffers.IsEmpty())
        return false;

    TextureBuffer* first = m_buffers[0];
    int w = first->m_info.m_width;
    int h = first->m_info.m_height;
    if (w <= 0 or h <= 0)
        return false;

    // Assemble the 6 faces (fewer buffers repeat the last one across the remaining faces — the
    // "same texture on several faces without reloading" case the loader supports).
    int faceCount = m_buffers.Length();
    const uint8_t* faces[6];
    for (int i = 0; i < 6; ++i)
        faces[i] = m_buffers[i < faceCount ? i : faceCount - 1]->DataBuffer();

    const GfxPixelFormat fmt = first->m_info.m_gfxFormat;
    if (GfxIsBlockCompressed(fmt)) {
        const int mipCount = first->m_info.m_mipCount;
        if (not CreateTextureResource(w, h, 6, mipCount, ToDXGIFormat(fmt)))
            return false;
        if (not UploadCompressedData(dx12Context.Device(), m_resource.Get(), faces, 6, w, h, fmt, mipCount))
            return false;
    }
    else {
        if (not CreateTextureResource(w, h, 6))
            return false;
        if (not UploadTextureData(dx12Context.Device(), m_resource.Get(), faces, 6, w, h, 4))
            return false;
    }

    if (not CreateSRV())
        return false;

    m_isDeployed = true;
    return true;
}

// =================================================================================================
