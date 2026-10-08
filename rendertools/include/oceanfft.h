#pragma once

#include <cstdint>

#include "array.hpp"
#include "vector.hpp"
#include "rendertarget.h"
#include "texture.h"

class ComputeShader;

// =================================================================================================

class OceanFFT {
public:
    static constexpr int cCascadeCount = 3;
    static constexpr int cGridSize = 256;
    static constexpr int cLogSize = 8;
    static constexpr float cBandFactor = 6.0f;

    struct Wave {
        float   length{ 0.0f };
        float   heading{ 0.0f };
        float   spread{ 1.0f };
        float   height{ 0.0f };
        float   peakedness{ 3.3f };
    };

    struct Params {
        float       tileSizes[cCascadeCount]{ 1009.0f, 97.0f, 11.0f };
        Wave        swell{ 57.0f, 0.6f, 4.0f, 3.1f, 10.0f };
        Wave        windSea{ 20.0f, 1.1f, 1.5f, 0.56f, 3.3f };
        float       shortestWave{ 0.35f };
        float       gravity{ 9.81f };
        float       choppiness{ 1.0f };
        float       foamBias{ 0.9f };
        float       foamScale{ 2.5f };
        float       foamDecay{ 0.4f };
        uint32_t    seed{ 20261008u };
    };

    ~OceanFFT() {
        Destroy();
    }

    bool Create(const Params& params);

    void Destroy(void);

    bool Update(float time, float deltaTime);

    Texture* DisplacementMap(int cascade);

    Texture* DerivativeMap(int cascade);

    inline float TileSize(int cascade) const noexcept {
        return m_params.tileSizes[cascade];
    }

    inline float ShortestWave(int cascade) const noexcept {
        return (cascade + 1 < cCascadeCount) ? m_params.tileSizes[cascade + 1] / cBandFactor : m_params.shortestWave;
    }

    inline const Params& Parameters(void) const noexcept {
        return m_params;
    }

    inline bool IsAvailable(void) const noexcept {
        return m_isAvailable;
    }

    inline bool IsReady(void) const noexcept {
        return m_isReady;
    }

private:
    static constexpr int cWorkBufferCount = 5;
    static constexpr uint32_t cTileSize = 8;

    Params          m_params;
    RenderTarget*   m_work{ nullptr };
    RenderTarget*   m_displacement[cCascadeCount][2]{};
    RenderTarget*   m_derivatives[cCascadeCount]{};
    int             m_current{ 0 };
    bool            m_isAvailable{ false };
    bool            m_hasSpectrum{ false };
    bool            m_isReady{ false };

    RenderTarget* CreateTarget(const char* name, int width, int height, int bufferCount, GfxPixelFormat format);

    void BuildSpectrum(Vector4f* spectrum) const;

    bool UploadSpectrum(void);

    bool Compute(float time, float deltaTime);

    void SetConstants(ComputeShader* shader, float time, float deltaTime) const;

    Texture* MapTexture(RenderTarget* target);
};

// =================================================================================================
