#pragma once

#include "base_noisetexture.h"
#include "oglupload.h"
#include "gfxstates.h"

// =================================================================================================
// OpenGL-specific noise texture subclasses. The class names match the historical API so consumers
// don't need to change. All common code (Allocate, Compute, ApplyWarp, mip downsampling, file I/O)
// lives in BaseNoiseTexture* / BaseCloudNoiseTexture / BaseBlueNoiseTexture; this header adds
// only the OGL-specific Deploy + SetParams overrides and the factories needed for the CloudNoise
// mip pyramid.

// -------------------------------------------------------------------------------------------------
// Translates the platform-neutral TextureSampling struct to glTexParameteri calls on the bound
// texture. When mipMode != None it additionally issues glGenerateMipmap (which the historical
// per-tag SetParams used to do for the mipmap-linear cases).
void ApplyTextureSamplingToGL(GLenum target, const TextureSampling& s) noexcept;

// =================================================================================================
// 2D templated noise (Perlin / FBM / Value / Hash / Weather / BlueNoise R8).

template<class Tag>
class NoiseTexture
    : public BaseNoiseTexture<Tag>
{
public:
    void SetParams(bool enforce) override {
        if (enforce or not this->m_hasParams) {
            this->m_hasParams = true;
            TextureSampling sampling;
            NoiseTraits<Tag>::ConfigureSampling(sampling);
            ApplyTextureSamplingToGL(GL_TEXTURE_2D, sampling);
        }
    }
};

using ValueNoiseTexture   = NoiseTexture<ValueNoiseR32F>;
using PerlinNoiseTexture  = NoiseTexture<PerlinNoiseR32F>;
using FbmNoiseTexture     = NoiseTexture<FbmNoiseR32F>;
using HashNoiseTexture    = NoiseTexture<HashNoiseRGBA8>;
using WeatherNoiseTexture = NoiseTexture<WeatherNoiseRG8>;

// =================================================================================================
// 3D noise textures.

class NoiseTexture3D
    : public BaseNoiseTexture3D
{
public:
    bool Deploy(int bufferIndex = 0) override;
    void SetParams(bool enforce = false) override;
};

// =================================================================================================

class CloudNoiseTexture
    : public BaseCloudNoiseTexture
{
public:
    bool Deploy(int bufferIndex = 0) override;
    void SetParams(bool enforce = false) override;

protected:
    BaseCloudNoiseTexture* NewMaxMipTex(void) override;
    BaseCloudNoiseTexture* NewAvgMipTex(void) override;
};

// =================================================================================================

class NoiseMaxMipTexture
    : public CloudNoiseTexture
{
public:
    void SetParams(bool enforce = false) override;
};

// =================================================================================================

class NoiseAvgMipTexture
    : public CloudNoiseTexture
{
public:
    void SetParams(bool enforce = false) override;
};

// =================================================================================================

class BlueNoiseTexture
    : public BaseBlueNoiseTexture
{
public:
    bool Deploy(int bufferIndex = 0) override;
    void SetParams(bool enforce = false) override;
};

// =================================================================================================
