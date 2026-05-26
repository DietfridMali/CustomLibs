#pragma once

#include "base_noisetexture.h"
#include "dx12upload.h"

// =================================================================================================
// DirectX 12-specific noise texture subclasses. Class names match the historical API. All common
// code lives in BaseNoiseTexture* / BaseCloudNoiseTexture / BaseBlueNoiseTexture; this header
// adds only the DX-specific Deploy + SetParams overrides plus the factory hooks for the
// CloudNoise mip pyramid.

// =================================================================================================
// 2D templated noise — DX subclass adds a SetParams override that writes m_sampling. The base
// template's SetParams stays trivial because the OGL Texture base has no m_sampling member.

template<class Tag>
class NoiseTexture
    : public BaseNoiseTexture<Tag>
{
public:
    void SetParams(bool enforce) override {
        if (enforce or not this->m_hasParams) {
            this->m_hasParams = true;
            NoiseTraits<Tag>::ConfigureSampling(this->m_sampling);
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
