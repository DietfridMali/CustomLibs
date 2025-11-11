#pragma once

#include <algorithm>

// =================================================================================================

struct FBMParams {
    float frequency{ 1.f };
    float lacunarity{ 2.f };
    float initialGain{ .5f };
    float gain{ .5f };
    int octaves{ 5 };
    int perturb{ 0 };
};

template<typename NoiseFn>
class FBM {
private:
    NoiseFn     m_noiseFn;
    FBMParams   m_params;
    float       m_normal;

    float Compute(Vector3f p) const {
        float n = 0.f;
        float a = m_params.initialGain;
        float f = m_params.frequency;
        p *= f;
        for (int i = 0; i < m_params.octaves; ++i) {
            float v = m_noiseFn(p);
            n += a * (m_params.perturb ? (v < 0) ? -v : v : v);
            a *= m_params.gain;
            p *= m_params.lacunarity;
        }
        return n;
    }

public:
    FBM(const NoiseFn& noiseFn, const FBMParams& params) 
        : m_noiseFn(noiseFn), m_params(params) 
    {
        float a = params.initialGain;
        m_normal = 0.f;
        for (int i = 0; i < params.octaves; ++i) {
            m_normal += a;
            a *= params.gain;
        }
        if (m_normal <= 0.f)
            m_normal = 1.f;
    }

    float Value(Vector3f& p) const {
        float n = Compute(p) / m_normal;
        if (not m_params.perturb)
            n = 0.5f + 0.5f * n;
        else if (m_params.perturb < 0)
            n = 1.0f - n;
        return std::clamp(n, 0.0f, 1.0f);
    }
};

// =================================================================================================
