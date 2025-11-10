#pragma once

// =================================================================================================

struct FBMParams {
    float frequency{ 1.f };
    float lacunarity{ 2.f };
    float initialGain{ .6f };
    float gain{ .5f };
    int octaves{ 5 };
};

template<typename NoiseFn>
class FBM {
private:
    NoiseFn     m_noiseFn;
    FBMParams   m_params;
    float       m_normal;

    float Compute(float x, float y, float z) const {
        float v = 0.f;
        float a = m_params.initialGain;
        float f = m_params.frequency;
        for (int i = 0; i < m_params.octaves; i++) {
            v += a * m_noise(x * f, y * f, z * f);
            a *= m_params.gain;
            f *= m_params.lacunarity;
        }
        return v;
    }

public:
    FBM(const NoiseFn& f, const FBMParams& p) 
        : m_noise(f), m_params(p) 
    {
        float a = p.initialGain;
        m_normal = 0.f;
        for (int i = 0; i < p.octaves; i++) {
            m_normal += a;
            a *= p.gain;
        }
        if (m_normal <= 0.f)
            m_normal = 1.f;
    }

    float Value(float x, float y, float z) const {
        float n = 0.5f + 0.5f * Compute(x, y, z) / m_normal;
        return std::clamp(n, 0.0f, 1.0f);
    }
};

// =================================================================================================
