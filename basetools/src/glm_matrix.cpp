
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


static bool NormalizeVec(glm::vec3& v) noexcept {
    float l = glm::length(v);
    if (l == 0.0f)
        return false;
    v /= l;
    return true;
}


// right and up for a forward vector that carries no other information
static void BasisFromForward(glm::vec3& r, glm::vec3& u, const glm::vec3& f) noexcept {
    if ((f.x == 0.0f) and (f.z == 0.0f)) { // straight up or down
        r = glm::vec3(1.0f, 0.0f, 0.0f);
        u = glm::vec3(0.0f, 0.0f, (f.y < 0.0f) ? 1.0f : -1.0f);
    }
    else {
        r = glm::vec3(f.z, 0.0f, -f.x);
        NormalizeVec(r);
        u = glm::cross(f, r);
    }
}


Matrix4f Matrix4f::Create(const Vector3f& fVec, const Vector3f& uVec, const Vector3f& rVec) noexcept {
    glm::vec3 f(fVec), u(0.0f), r(0.0f);

    NormalizeVec(f);
    if (not uVec.IsZero()) {
        u = glm::vec3(uVec);
        if (not NormalizeVec(u))
            BasisFromForward(r, u, f);
        r = glm::cross(u, f);
        if (not NormalizeVec(r))
            BasisFromForward(r, u, f);
        u = glm::cross(f, r);
    }
    else if (not rVec.IsZero()) {
        r = glm::vec3(rVec);
        if (not NormalizeVec(r))
            BasisFromForward(r, u, f);
        u = glm::cross(f, r);
        if (not NormalizeVec(u))
            BasisFromForward(r, u, f);
        r = glm::cross(u, f);
    }
    else
        BasisFromForward(r, u, f);

    Matrix4f m;
    m.m[0] = glm::vec4(r, 0.0f);
    m.m[1] = glm::vec4(u, 0.0f);
    m.m[2] = glm::vec4(f, 0.0f);
    m.m[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return m;
}


Matrix4f& Matrix4f::EulerComputeYXZ(float sinX, float cosX, float sinY, float cosY, float sinZ, float cosZ, bool transpose) noexcept {
    // Descent/D2 rotation order Ry(heading) * Rx(pitch) * Rz(bank), X=pitch, Y=heading, Z=bank.
    // The three axis vectors are the ones CFixMatrix::Create computes (r, u, f).
    // transpose = true for camera matrices, false for object orientations
    const float sbsh = sinZ * sinY;
    const float cbch = cosZ * cosY;
    const float cbsh = cosZ * sinY;
    const float sbch = sinZ * cosY;

    const glm::vec3 r(cbch + sinX * sbsh, sinZ * cosX, sinX * sbch - cbsh);
    const glm::vec3 u(sinX * cbsh - sbch, cosZ * cosX, sbsh + sinX * cbch);
    const glm::vec3 f(sinY * cosX, -sinX, cosY * cosX);

    if (transpose) {
        m[0] = glm::vec4(r.x, u.x, f.x, 0.0f);
        m[1] = glm::vec4(r.y, u.y, f.y, 0.0f);
        m[2] = glm::vec4(r.z, u.z, f.z, 0.0f);
    }
    else {
        m[0] = glm::vec4(r, 0.0f);
        m[1] = glm::vec4(u, 0.0f);
        m[2] = glm::vec4(f, 0.0f);
    }
    m[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
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