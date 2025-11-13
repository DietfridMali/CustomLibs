
#include <algorithm>
#include "glew.h"
#include "conversions.hpp"
#include "projection.h"

// =================================================================================================

Matrix4f Projection::Create(float aspectRatio, float fov, bool rowMajor)
noexcept
{
    m_aspectRatio = aspectRatio;
    m_fov = fov;
    return ComputeProjection(rowMajor);
}


Matrix4f Projection::ComputeProjection(bool rowMajor)
noexcept
{
#if USE_GLM
    float radFov = glm::radians(m_fov);
    return Matrix4f(glm::perspective(glm::radians(m_fov), m_aspectRatio, m_zNear, m_zFar));
#else
    float yMax = m_zNear * tanf(Conversions::DegToRad(m_fov / 2));
    float xMax = yMax * m_aspectRatio;
    return ComputeFrustum(-xMax, xMax, -yMax, yMax, rowMajor);
#endif
}


Matrix4f Projection::ComputeFrustum(float left, float right, float bottom, float top, bool rowMajor)
noexcept
{
#if USE_GLM
    return Matrix4f();
#else
    float nearPlane = 2.0f * m_zNear;
    float depth = m_zFar - m_zNear;

    float width = right - left;
    float height = top - bottom;
    Matrix4f m({
        Vector4f{ nearPlane / width,               0.0f,        (left + right) / width,  0.0f },
        Vector4f{              0.0f, nearPlane / height,       (top + bottom) / height,  0.0f },
        Vector4f{              0.0f,               0.0f,   -(m_zFar + m_zNear) / depth, -1.0f },
        Vector4f{              0.0f,               0.0f, (-nearPlane * m_zFar) / depth,  0.0f }
        }, false);
    return rowMajor ? m.Transpose() : m;
#endif
}


Matrix4f Projection::ComputeOrthoProjection(float left, float right, float bottom, float top, float zNear, float zFar, bool rowMajor)
noexcept
{
#if USE_GLM
#   if 0
    Matrix4f m(glm::ortho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0));
#       if 0 // erst aufrufenden Code überprüfen!
    Matrix4f m(glm::ortho(left, right, bottom, top, zNear, zFar));
#       endif
    return m;
#else
    m = Matrix4f({ Vector4f{ 2.0f,  0.0f,  0.0f, -1.0f },  // erste Zeile
                   Vector4f{ 0.0f,  2.0f,  0.0f, -1.0f },  // zweite Zeile
                   Vector4f{ 0.0f,  0.0f, -1.0f,  0.0f },  // dritte Zeile
                   Vector4f{ 0.0f,  0.0f,  0.0f,  1.0f }   // vierte Zeile
        });
#   endif
#else
    Matrix4f m({ Vector4f{ 2.0f,  0.0f,  0.0f,  0.0f },  // erste Zeile
                  Vector4f{ 0.0f,  2.0f,  0.0f,  0.0f },  // zweite Zeile
                  Vector4f{ 0.0f,  0.0f, -1.0f,  0.0f },  // dritte Zeile
                  Vector4f{-1.0f, -1.0f,  0.0f,  1.0f }   // vierte Zeile
        },
        false);
#endif
#if 0
    float rl = right - left;
    float tb = top - bottom;
    float fn = zFar - zNear;

    return Matrix4f({
        Vector4f({  2.0f / rl, 0.0f,        0.0f,         -(right + left) / rl }),
        Vector4f({  0.0f,      2.0f / tb,   0.0f,         -(top + bottom) / tb }),
        Vector4f({  0.0f,      0.0f,       -2.0f / fn,    -(zFar + zNear) / fn }),
        Vector4f({  0.0f,      0.0f,        0.0f,          1.0f })
        });
#endif
    return rowMajor ? m : m.Transpose();
}

// =================================================================================================
