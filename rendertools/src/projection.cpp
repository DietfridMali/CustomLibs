
#include <algorithm>
#include "glew.h"
#include "conversions.hpp"
#include "projection.h"

// =================================================================================================

Matrix4f Projection::Create(float aspectRatio, float fov, bool rowMajor)
{
    m_aspectRatio = aspectRatio;
    m_fov = fov;
    return ComputeProjection(rowMajor);
}


Matrix4f Projection::ComputeProjection(bool rowMajor)
{
    //m_projection.Create (16);
    float yMax = m_zNear * tanf(Conversions::DegToRad(m_fov / 2));
    //ymin = -ymax;
    //xmin = -ymax * aspectRatio;
    float xMax = yMax * m_aspectRatio;
    return ComputeFrustum(-xMax, xMax, -yMax, yMax, rowMajor);
}


Matrix4f Projection::ComputeFrustum(float left, float right, float bottom, float top, bool rowMajor)
{
    float nearPlane = 2.0f * m_zNear;
    float depth = m_zFar - m_zNear;

#if 0
    // symmetric frustum, i.e. left == -right and bottom == -top
    m_projection =
        Matrix4f(
            Vector4f{m_zNear / right, 0.0f, 0.0f, 0.0f},
            Vector4f{0.0f, m_zNear / top, 0.0f, 0.0f},
            Vector4f{0.0f, 0.0f, -{m_zFar + m_zNear} / depth, -1.0f},
            Vector4f{0.0f, 0.0f, {-nearPlane * m_zFar} / depth, 0.0f}
        );
#else
    float width = right - left;
    float height = top - bottom;
#if USE_GLM
    Matrix4f m({
        Vector4f{ nearPlane / width,               0.0f,        (left + right) / width,  0.0f },
        Vector4f{              0.0f, nearPlane / height,       (top + bottom) / height,  0.0f },
        Vector4f{              0.0f,               0.0f,   -(m_zFar + m_zNear) / depth, -1.0f },
        Vector4f{              0.0f,               0.0f, (-nearPlane * m_zFar) / depth,  0.0f }
        });
    return m;
#else
    Matrix4f m ({
        Vector4f{ nearPlane / width,               0.0f,        (left + right) / width,  0.0f },
        Vector4f{              0.0f, nearPlane / height,       (top + bottom) / height,  0.0f },
        Vector4f{              0.0f,               0.0f,   -(m_zFar + m_zNear) / depth, -1.0f },
        Vector4f{              0.0f,               0.0f, (-nearPlane * m_zFar) / depth,  0.0f }
        }, false);
    return rowMajor ? m.Transpose() : m;
#endif
#endif
}


Matrix4f Projection::ComputeOrthoProjection(float left, float right, float bottom, float top, float zNear, float zFar, bool rowMajor) {
#if USE_GLM
#   if 1
    Matrix4f m (glm::ortho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0));
    return m;
#else
    m = Matrix4f({ Vector4f{ 2.0f,  0.0f,  0.0f, -1.0f },  // erste Zeile
                 Vector4f{ 0.0f,  2.0f,  0.0f, -1.0f },  // zweite Zeile
                 Vector4f{ 0.0f,  0.0f, -1.0f,  0.0f },  // dritte Zeile
                 Vector4f{ 0.0f,  0.0f,  0.0f,  1.0f }   // vierte Zeile
        });
#   endif
#else
    Matrix4f m ({ Vector4f{ 2.0f,  0.0f,  0.0f,  0.0f },  // erste Zeile
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
