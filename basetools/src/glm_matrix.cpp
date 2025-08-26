
#if defined(COMPILE_MATRIX)

#include "glm_matrix.hpp"

const Matrix4f Matrix4f::IDENTITY = Matrix4f(glm::mat4(1.0f));


Matrix4f Matrix4f::Scaling(float sx, float sy, float sz) noexcept {
    return Matrix4f({ 
        sx, 0,  0,  0,
        0,  sy, 0,  0,
        0,  0,  sz, 0,
        0,  0,  0,  1 
    });
}


Matrix4f Matrix4f::Translation(float dx, float dy, float dz) noexcept {
    return Matrix4f({ 
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
          dx,   dy,   dz, 1.0f 
    });
}


Matrix4f& Matrix4f::EulerComputeZYX(float sinX, float cosX, float sinY, float cosY, float sinZ, float cosZ) noexcept {
    m[0][0] = cosZ * cosY;
    m[0][1] = cosZ * sinY * sinX - sinZ * cosX;
    m[0][2] = cosZ * sinY * cosX + sinZ * sinX;
    m[0][3] = 0.0f;

    m[1][0] = sinZ * cosY;
    m[1][1] = sinZ * sinY * sinX + cosZ * cosX;
    m[1][2] = sinZ * sinY * cosX - cosZ * sinX;
    m[1][3] = 0.0f;

    m[2][0] = -sinY;
    m[2][1] = cosY * sinX;
    m[2][2] = cosY * cosX;
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
    return *this;
}


Matrix4f Matrix4f::AffineInverse(void) noexcept {
    glm::mat3 r(m);
    glm::mat3 ri = glm::inverse(r);
    glm::vec3 xlat = -ri * glm::vec3(m[3]);
    glm::mat4 i = glm::mat4(1.0f);
    i = glm::mat4(ri);
    i[3] = glm::vec4(xlat, 1.0f);
    return Matrix4f(i);
}

#endif //USE_GLM