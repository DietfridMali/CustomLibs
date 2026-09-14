#include "oglupload.h"

#include "texture.h"
#include "gfxpixelformat_gl.h"

// =================================================================================================

static inline GLint UnpackAlignmentFor(GfxPixelFormat fmt) noexcept {
    return (GfxPixelStride(fmt) >= 4) ? GLint(4) : GLint(1);
}

// =================================================================================================

bool Upload2DTexture(Texture& tex, int width, int height,
                     GfxPixelFormat fmt, const void* data) noexcept
{
    if (tex.IsDeployed())
        return true;
    if (not tex.Bind(0, true))
        return false;

    const GfxPixelFormat encodedFmt = GfxEncodedFormat(fmt, tex.m_colorEncoding);
    const GLFormat glf = ToGLFormat(encodedFmt);
    glPixelStorei(GL_UNPACK_ALIGNMENT, UnpackAlignmentFor(fmt));
    glTexImage2D(GL_TEXTURE_2D, 0,
                 glf.internalFormat,
                 width, height, 0,
                 glf.externalFormat, glf.type,
                 data);
    tex.m_mipChainLength = 0;
    if (tex.m_useMipMaps and (encodedFmt != GfxLinearFormat(encodedFmt)))
        tex.UploadSRGBMipChain(glf.internalFormat, glf.externalFormat, reinterpret_cast<const uint8_t*>(data), width, height, int(GfxPixelStride(fmt)));
    // SetParams is virtual — the concrete NoiseTexture override may additionally call
    // glGenerateMipmap for tags that request mipmap-linear filtering.
    tex.SetParams(false);
    tex.Release();
    tex.m_isDeployed = true;
    return true;
}

bool Upload3DTexture(Texture& tex, int width, int height, int depth,
                     GfxPixelFormat fmt, const void* data,
                     bool generateMips) noexcept
{
    if (tex.IsDeployed())
        return true;
    if (not tex.Bind(0, true))
        return false;

    const GLFormat glf = ToGLFormat(fmt);
    glPixelStorei(GL_UNPACK_ALIGNMENT, UnpackAlignmentFor(fmt));
    glTexImage3D(GL_TEXTURE_3D, 0,
                 glf.internalFormat,
                 width, height, depth, 0,
                 glf.externalFormat, glf.type,
                 data);
    tex.SetParams(false);
    if (generateMips)
        glGenerateMipmap(GL_TEXTURE_3D);
    tex.Release();
    tex.m_isDeployed = true;
    return true;
}

// =================================================================================================
