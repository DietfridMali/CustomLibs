#pragma once

// =================================================================================================

class Projection {
public:
    float   m_aspectRatio;
    float   m_fov;
    float   m_zNear;
    float   m_zFar;
    float   m_zoom;

    Projection()
        : m_zNear(0.1f), m_zFar(1000.0f), m_zoom(1.0f), m_aspectRatio(1920.0f / 1080.0f), m_fov(90.0f)
    {
    }

    Matrix4f Create(float aspectRatio, float fov = 45, bool rowMajor = false)
        noexcept;

    Matrix4f ComputeProjection(bool rowMajor = false)
        noexcept;

    Matrix4f ComputeOrthoProjection(float left, float right, float bottom, float top, float zNear, float zFar, bool rowMajor = false)
        noexcept;

    Matrix4f ComputeFrustum(float left, float right, float bottom, float top, bool rowMajor = false)
        noexcept;
};

// =================================================================================================
