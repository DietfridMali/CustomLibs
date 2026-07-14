#include "cubemap.h"
#include "gfxpixelformat_gl.h"

// =================================================================================================
// Load cubemap textures from file and generate an OpenGL cubemap

// Upload one cubemap face: block-compressed formats push each DDS mip level via
// glCompressedTexImage2D; uncompressed formats use a single glTexImage2D (level 0).
static void UploadCubeFace(GLenum target, TextureBuffer* buf) {
    const GfxPixelFormat gfxFmt = buf->m_info.m_gfxFormat;
    if (GfxIsBlockCompressed(gfxFmt)) {
        const GLenum   internalFormat = ToGLFormat(gfxFmt).internalFormat;
        const uint32_t blockBytes     = GfxBlockBytes(gfxFmt);
        const uint8_t* level          = reinterpret_cast<const uint8_t*>(buf->m_data.DataPtr());
        int w = buf->m_info.m_width, h = buf->m_info.m_height;
        for (int mip = 0; mip < buf->m_info.m_mipCount; ++mip) {
            const GLsizei imgSize = GLsizei(uint32_t((w + 3) / 4) * uint32_t((h + 3) / 4) * blockBytes);
            glCompressedTexImage2D(target, mip, internalFormat, w, h, 0, imgSize, level);
            level += imgSize;
            w = (w > 1) ? (w >> 1) : 1;
            h = (h > 1) ? (h >> 1) : 1;
        }
    }
    else {
        glTexImage2D(target, 0, buf->m_info.m_internalFormat, buf->m_info.m_width, buf->m_info.m_height, 0,
                     buf->m_info.m_format, GL_UNSIGNED_BYTE, buf->m_data.DataPtr());
    }
}


void Cubemap::SetParams(void) {
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}


bool Cubemap::Deploy(int bufferIndex) {
    if (IsDeployed())
        return true;
    if (not Bind(0, true))
        return false;
    SetParams();
    int i = 0;
    // put the available textures on the cubemap as far as possible and put the last texture on any remaining cubemap faces
    // Reguar case six textures: One texture for each cubemap face
    // Special case one textures: all cubemap faces bear the same texture
    // Special case two textures: first texture goes to first 5 cubemap faces, 2nd texture goes to 6th cubemap face. Special case for smileys with a uniform skin and a face
    TextureBuffer* texBuf = nullptr;
    for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
        texBuf = *it;
        UploadCubeFace(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i), texBuf);
        ++i;
    }
    if (texBuf) {
        for (; i < 6; i++)
            UploadCubeFace(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i), texBuf);
    }
    Release ();
    m_isDeployed = true;
    return true;
}

// =================================================================================================
