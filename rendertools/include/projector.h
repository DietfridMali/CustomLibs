#pragma once

#include "matrix.hpp"

// =================================================================================================

class Projector {
public:
    float   m_aspectRatio;
    float   m_fov;
    float   m_zNear;
    float   m_zFar;
    float   m_zoom;

    Projector(float aspectRatio = 1920.0f / 1080.0f, float fov = 45, float zNear = 0.1f, float zFar = 100.0f, float zoom = 1.0f)
        : m_aspectRatio(aspectRatio), m_fov(fov), m_zNear(zNear), m_zFar(zFar), m_zoom(zoom)
    {
    }

    void Setup(float aspectRatio, float fov = 45, float zNear = 0.0f, float zFar = 0.0f, float zoom = 1.0f)
        noexcept;

    Matrix4f Compute3DProjection(bool rowMajor = false)
        noexcept;

    Matrix4f ComputeOrthoProjection(float left, float right, float bottom, float top, float zNear, float zFar, bool rowMajor = false)
        noexcept;

    Matrix4f ComputeFrustum(float left, float right, float bottom, float top, bool rowMajor = false)
        noexcept;

    inline float ZNear(void) noexcept {
        return m_zNear;
    }

    inline float ZFar(void) noexcept {
        return m_zFar;
    } 

    inline float FoV(void) noexcept {
        return m_fov;
    }

    inline float AspectRatio(void) noexcept {
        return m_aspectRatio;
    }
};

// =================================================================================================
