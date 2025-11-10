#pragma once

// =================================================================================================

struct FBMParams {
    float frequency;
    float lacunarity;
    float initialGain;
    float gain;
    int octaves;
};

template<typename NoiseFn>
class FBM {
private:
    NoiseFn     m_noiseFn;
    FBMParams   m_params;
    float       m_normal;

    float Compute(float x, float y, float z) const {
        float v = 0.f;
        float a = params.initialGain;
        float f = params.frequency;
        for (int i = 0; i < params.octaves; i++) {
            v += a * noise(x * f, y * f, z * f);
            a *= params.gain;
            f *= params.lacunarity;
        }
        return v;
    }

public:
    FBM(const NoiseFn& f, const FBMParams& p) :noise(f), params(p) {
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
        n = 0.5f + 0.5f * Compute(x, y, z) / m_normal;
        return std::clamp(n, 0.0f, 1.0f);
    }
};

// =================================================================================================
