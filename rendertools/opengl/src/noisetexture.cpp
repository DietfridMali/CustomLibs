#include "noisetexture.h"
#include "texture.h"

// =================================================================================================
// OpenGL-specific Deploy + SetParams overrides. All compute / mipmap / file I/O code lives in the
// common base_noisetexture.cpp; this unit only configures the GL sampler state and routes the
// Deploy call through the platform-neutral Upload<2D,3D>Texture helpers.

void ApplyTextureSamplingToGL(GLenum target, const TextureSampling& s) noexcept
{
    GLint minFilter = GL_LINEAR;
    GLint magFilter = GL_LINEAR;

    if (s.minFilter == GfxFilterMode::Nearest) {
        minFilter = 
            (s.mipMode == GfxMipMode::None)
            ? GL_NEAREST
            : (s.mipMode == GfxMipMode::Nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR);
    }
    else {
        minFilter = 
            (s.mipMode == GfxMipMode::None)
            ? GL_LINEAR
            : (s.mipMode == GfxMipMode::Nearest ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
    }

    magFilter = (s.magFilter == GfxFilterMode::Nearest) ? GL_NEAREST : GL_LINEAR;

    auto wrap = [](GfxWrapMode m) -> GLint {
        return (m == GfxWrapMode::Repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    };

    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap(s.wrapU));
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap(s.wrapV));
    glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap(s.wrapW));

    if (s.mipMode != GfxMipMode::None) {
        glGenerateMipmap(target);
    }
    else {
        glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
    }
}

// =================================================================================================
// NoiseTexture3D

bool NoiseTexture3D::Deploy(int) {
    return Upload3DTexture(*this, m_gridDimensions.x, m_gridDimensions.y, m_gridDimensions.z, GfxPixelFormat::RGBA16_SFloat, reinterpret_cast<const void*>(m_data.DataPtr()));
}


void NoiseTexture3D::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

// =================================================================================================
// CloudNoiseTexture + mip variants

bool CloudNoiseTexture::Deploy(int) {
    // Hardware-Mip-Chain auf der 256³ Shape-Noise erzeugen: ersetzt die separate CPU-vorberechnete
    // AvgMip-Pyramide (shapeNoiseAvgMip1..4Tex) durch eine lückenlose, statistisch kohärente
    // Mip-Folge ab dem Original. Distance-LOD im Shader läuft jetzt über ein einziges textureLod
    // mit lodFloat-Mip-Bias statt zweier separater Samples + manuellem mix.
    return Upload3DTexture(*this, m_gridSize, m_gridSize, m_gridSize, GfxPixelFormat::R16_SFloat, reinterpret_cast<const void*>(m_data.DataPtr()), true);
}


void CloudNoiseTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        // GL_LINEAR_MIPMAP_LINEAR → trilineares Sampling über die Hardware-Mip-Chain.
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}


BaseCloudNoiseTexture* CloudNoiseTexture::NewMaxMipTex(void) {
    return new NoiseMaxMipTexture();
}


BaseCloudNoiseTexture* CloudNoiseTexture::NewAvgMipTex(void) {
    return new NoiseAvgMipTexture();
}


void NoiseMaxMipTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}


void NoiseAvgMipTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

// =================================================================================================
// DetailNoiseTexture

bool DetailNoiseTexture::Deploy(int) {
    return Upload3DTexture(*this, m_gridSize, m_gridSize, m_gridSize, GfxPixelFormat::R8_UNorm, reinterpret_cast<const void*>(m_data.DataPtr()));
}


void DetailNoiseTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

// =================================================================================================
// BlueNoiseTexture

bool BlueNoiseTexture::Deploy(int) {
    return Upload3DTexture(*this, m_gridSize.x, m_gridSize.y, 64, GfxPixelFormat::R8_UNorm, reinterpret_cast<const void*>(m_data.DataPtr()));
}


void BlueNoiseTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

// =================================================================================================
