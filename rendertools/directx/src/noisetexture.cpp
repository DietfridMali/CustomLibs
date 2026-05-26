#define NOMINMAX

#include "noisetexture.h"
#include "texture.h"

// =================================================================================================
// DirectX 12-specific Deploy + SetParams overrides. All compute / mipmap / file I/O code lives in
// the common base_noisetexture.cpp; this unit only configures the DX sampler state and routes
// Deploy through the platform-neutral Upload<2D,3D>Texture helpers in dx12upload.

// -------------------------------------------------------------------------------------------------
// Common sampler setup helper.
static void ConfigureSamplingForNoise(TextureSampling& s, GfxFilterMode minMag, GfxMipMode mip) noexcept
{
    s.minFilter = minMag;
    s.magFilter = minMag;
    s.mipMode = mip;
    s.wrapU = s.wrapV = s.wrapW = GfxWrapMode::Repeat;
    s.compareFunc = GfxOperations::CompareFunc::Always;
    s.maxAnisotropy = 1.0f;
}

// =================================================================================================
// NoiseTexture3D — RGBA float cloud noise source

bool NoiseTexture3D::Deploy(int) {
    return Upload3DTexture(*this, m_gridDimensions.x, m_gridDimensions.y, m_gridDimensions.z, GfxPixelFormat::RGBA32_SFloat, reinterpret_cast<const void*>(m_data.Data()));
}


void NoiseTexture3D::SetParams(bool) {
    m_hasParams = true;
    ConfigureSamplingForNoise(m_sampling, GfxFilterMode::Linear, GfxMipMode::None);
}

// =================================================================================================
// CloudNoiseTexture + mip variants

bool CloudNoiseTexture::Deploy(int) {
    return Upload3DTexture(*this, m_gridSize, m_gridSize, m_gridSize, GfxPixelFormat::R32_SFloat, reinterpret_cast<const void*>(m_data.Data()));
}


void CloudNoiseTexture::SetParams(bool) {
    m_hasParams = true;
    ConfigureSamplingForNoise(m_sampling, GfxFilterMode::Linear, GfxMipMode::None);
}


BaseCloudNoiseTexture* CloudNoiseTexture::NewMaxMipTex(void) {
    return new NoiseMaxMipTexture();
}


BaseCloudNoiseTexture* CloudNoiseTexture::NewAvgMipTex(void) {
    return new NoiseAvgMipTexture();
}


void NoiseMaxMipTexture::SetParams(bool) {
    m_hasParams = true;
    ConfigureSamplingForNoise(m_sampling, GfxFilterMode::Nearest, GfxMipMode::None);
}


void NoiseAvgMipTexture::SetParams(bool) {
    m_hasParams = true;
    ConfigureSamplingForNoise(m_sampling, GfxFilterMode::Linear, GfxMipMode::None);
}

// =================================================================================================
// BlueNoiseTexture

bool BlueNoiseTexture::Deploy(int) {
    return Upload3DTexture(*this, 128, 128, 64, GfxPixelFormat::R8_UNorm, reinterpret_cast<const void*>(m_data.Data()));
}


void BlueNoiseTexture::SetParams(bool) {
    m_hasParams = true;
    ConfigureSamplingForNoise(m_sampling, GfxFilterMode::Nearest, GfxMipMode::None);
}

// =================================================================================================
