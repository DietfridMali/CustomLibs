#pragma once

#include <cstdint>

#include "matrix.hpp"
#include "vector.hpp"

// =================================================================================================

class ViewFrustum
{
public:
    static constexpr int32_t    cPlaneCount = 4;

    Vector4f    m_planes[cPlaneCount];

    ViewFrustum() = default;

    explicit ViewFrustum(const Matrix4f& clipTransform) noexcept {
        Setup(clipTransform);
    }

    void Setup(const Matrix4f& clipTransform) noexcept {
        const float* m = clipTransform.AsArray();
        for (int32_t i = 0; i < cPlaneCount; ++i) {
            int32_t axis = i / 2;
            float sign = ((i % 2) == 0) ? 1.0f : -1.0f;
            Vector3f normal(m[3] + sign * m[axis], m[7] + sign * m[4 + axis], m[11] + sign * m[8 + axis]);
            float scale = 1.0f / normal.Length();
            m_planes[i] = Vector4f(normal.x * scale, normal.y * scale, normal.z * scale, (m[15] + sign * m[12 + axis]) * scale);
        }
    }

    inline bool Contains(const Vector3f& center, float radius) const noexcept {
        for (int32_t i = 0; i < cPlaneCount; ++i) {
            if (m_planes[i].x * center.x + m_planes[i].y * center.y + m_planes[i].z * center.z + m_planes[i].w < -radius)
                return false;
        }
        return true;
    }
};

// =================================================================================================
