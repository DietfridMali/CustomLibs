#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <initializer_list>
#include <algorithm>
#include "conversions.hpp"
#include "glm_vector.hpp"

class Matrix4f {
public:
    glm::mat4 m;

    static const    Matrix4f IDENTITY;

    // =============================
    // Konstruktoren
    // =============================

    inline Matrix4f() : m(1.0f) {}

    inline Matrix4f(const glm::mat4& mat) : m(mat) {}

    inline Matrix4f(const float* data) {
        m = glm::make_mat4(data);
    }

    inline Matrix4f(std::initializer_list<float> list) {
        float data[16] = {};
        std::copy(list.begin(), list.end(), data);
        m = glm::make_mat4(data);
    }

    // =============================
    // FromArray
    // =============================

    inline Matrix4f& FromArray(const float* data = nullptr) {
        if (data) {
            m = glm::make_mat4(data);
        }
        return *this;
    }

    // =============================
    // AsArray
    // =============================

    inline const float* AsArray() const {
        return glm::value_ptr(m);
    }

    inline operator glm::mat4& () {
        return m;
    }

    inline operator const glm::mat4& () const {
        return m;
    }

    // =============================
    // EulerComputeZYX
    // =============================

    Matrix4f& EulerComputeZYX(float sinX, float cosX, float sinY, float cosY, float sinZ, float cosZ);

    // =============================
    // Transformationen
    // =============================

    static Matrix4f Identity() {
        return Matrix4f(glm::mat4(1.0f));
    }

    static Matrix4f Translation(float dx, float dy, float dz);

    static Matrix4f Translation(const Vector3f& v) {
        return Translation(v.X(), v.Y(), v.Z());
    }

    static Matrix4f Scaling(float sx, float sy, float sz);

    static Matrix4f Scaling(const Vector3f& s) {
        return Scaling(s.X(), s.Y(), s.Z());
    }

    static Matrix4f Rotation(float angleDeg, float x, float y, float z) {
        return Matrix4f(glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(x, y, z)));
    }

    static Matrix4f Rotation(Matrix4f& r, float angleDeg, float x, float y, float z) {
        r = Matrix4f(glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(x, y, z)));
        return r;
    }

    static Matrix4f Rotation(float angleDeg, const Vector3f& axis) {
        return Rotation(angleDeg, axis.X(), axis.Y(), axis.Z());
    }

    static inline Matrix4f& Rotation(Matrix4f& rotation, float x, float y, float z) {
        float radX = Conversions::DegToRad(x);
        float radY = Conversions::DegToRad(y);
        float radZ = Conversions::DegToRad(z);
        return rotation.EulerComputeZYX(sin(radX), cos(radX), sin(radY), cos(radY), sin(radZ), cos(radZ));
    }

    static inline Matrix4f Rotation(float x, float y, float z) {
        float radX = Conversions::DegToRad(x);
        float radY = Conversions::DegToRad(y);
        float radZ = Conversions::DegToRad(z);
        Matrix4f rotation;
        return Rotation(rotation, x, y, z);
    }

    static inline Matrix4f Rotation(Matrix4f& rotation, Vector3f angles) {
        return Rotation(rotation, angles.X(), angles.Y(), angles.Z());
    }

    static inline Matrix4f Rotation(Vector3f angles) {
        Matrix4f rotation;
        return Rotation(rotation, angles.X(), angles.Y(), angles.Z());
    }

    inline Matrix4f& Translate(float x, float y, float z) {
        m *= Translation(x, y, z).m;
        return *this;
    }

    inline Matrix4f& Translate(const Vector3f& v) {
        return Translate(v.X(), v.Y(), v.Z());
    }

    inline Matrix4f& Scale(float sx, float sy, float sz) {
        m = glm::scale(m, glm::vec3(sx, sy, sz));
        return *this;
    }

    inline Matrix4f& Scale(const glm::vec3& s) {
        m = glm::scale(m, s);
        return *this;
    }

    inline Matrix4f& Rotate(float angleDeg, const glm::vec3& axis) {
        if ((angleDeg != 0.0f) and (glm::length(axis) != 0.0f))
            m = glm::rotate(m, glm::radians(angleDeg), axis);
        return *this;
    }

    inline Matrix4f& Rotate(float angleDeg, float x, float y, float z) {
        return Rotate (angleDeg, glm::vec3(x, y, z));
        return *this;
    }

    template<typename T>  requires std::same_as<std::decay_t<T>, Matrix4f>
    Matrix4f& Rotate(T&& r) {
        m *= std::forward<T>(r).m;
        return *this;
    }

    // =============================
    // Transpose, Inverse, Determinant
    // =============================

    inline Matrix4f Transpose() const {
        return Matrix4f(glm::transpose(m));
    }

    inline Matrix4f Transpose(Matrix4f& m, int dimensions = 4) const {
        m = m.Transpose();
        return m;
    }

    inline Matrix4f Inverse() const {
        return Matrix4f(glm::inverse(m));
    }

    Matrix4f AffineInverse(void);

    static Vector3f Rotate(const Matrix4f& m, const Vector3f& v) {
        return m * v;
    }

    template <typename T> requires std::same_as<std::decay_t<T>, Vector3f>
    inline Vector3f Rotate(T&& v) const {
        return *this * std::forward<T>(v);
    }

    inline Vector3f Unrotate(const Vector3f v) {
        return Transpose() * v;
    }

    inline float Det() const {
        return glm::determinant(m);
    }

    // =============================
    // Operatoren
    // =============================

    inline Matrix4f operator*(const Matrix4f& other) const {
        return Matrix4f(m * other.m);
    }

    inline Matrix4f operator*(const glm::mat4& other) const {
        return Matrix4f(m * other);
    }

    inline Matrix4f& operator*=(const Matrix4f& other) {
        m *= other.m;
        return *this;
    }

    inline Matrix4f& operator*=(const glm::mat4& other) {
        m *= other;
        return *this;
    }

    Vector4f operator* (const Vector4f& v) const {
        return m * v;
    }

    Vector3f operator* (const Vector3f& v) const {
        Vector4f h = v;
        return m * h;
    }

    inline operator const float* () const {
        return glm::value_ptr(m);
    }

    // =============================
    // Utility-Funktionen
    // =============================

    inline glm::vec4& R() { return m[0]; }
    inline glm::vec4& U() { return m[1]; }
    inline glm::vec4& F() { return m[2]; }
    inline glm::vec4& T() { return m[3]; }

    inline const glm::vec4& R() const { return m[0]; }
    inline const glm::vec4& U() const { return m[1]; }
    inline const glm::vec4& F() const { return m[2]; }
    inline const glm::vec4& T() const { return m[3]; }

    inline const bool IsColMajor() {
        return true;
    }

    bool IsValid() const {
        const float* arr = glm::value_ptr(m);
        for (int i = 0; i < 16; ++i)
            if (arr[i] != arr[i]) // NaN check
                return false;
        return true;
    }
};
