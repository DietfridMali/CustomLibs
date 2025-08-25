#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <initializer_list>
#include <algorithm>
#include "conversions.hpp"
#include "glm_vector.hpp"

// =================================================================================================

class Matrix4f {
public:
    glm::mat4 m;

    static const Matrix4f IDENTITY;

    // ===== Special Members =====
    Matrix4f() noexcept(noexcept(glm::mat4(1.0f))) : m(1.0f) {}
    Matrix4f(const Matrix4f&) noexcept = default;
    Matrix4f(Matrix4f&&) noexcept = default;
    Matrix4f& operator=(const Matrix4f&) noexcept = default;
    Matrix4f& operator=(Matrix4f&&) noexcept = default;
    ~Matrix4f() noexcept = default;

    // ===== Constructors =====
    explicit Matrix4f(const glm::mat4& mat) noexcept : m(mat) {}

    explicit Matrix4f(const float* data)
        noexcept(noexcept(glm::make_mat4((const float*)nullptr)))
    {
        m = glm::make_mat4(data);
    }

    Matrix4f(std::initializer_list<float> list)
        noexcept(noexcept(glm::make_mat4((const float*)nullptr)))
    {
        float data[16] = {};
        // std::copy für Trivialtypen wirft praktisch nicht; wir koppeln die Garantie an make_mat4
        std::copy(list.begin(), list.end(), data);
        m = glm::make_mat4(data);
    }

    // ===== FromArray / AsArray =====
    Matrix4f& FromArray(const float* data = nullptr)
        noexcept(noexcept(glm::make_mat4((const float*)nullptr)))
    {
        if (data) m = glm::make_mat4(data);
        return *this;
    }

    const float* AsArray() const noexcept { return glm::value_ptr(m); }

    // Konvertierungen
    operator glm::mat4& () noexcept { return m; }
    operator const glm::mat4& () const noexcept { return m; }

    // ===== EulerComputeZYX =====
    Matrix4f& EulerComputeZYX(float sinX, float cosX, float sinY, float cosY, float sinZ, float cosZ); // (lassen ohne noexcept – hängt von deiner Implementierung ab)

    // ===== Static builders =====
    static Matrix4f Identity() noexcept(noexcept(glm::mat4(1.0f))) {
        return Matrix4f(glm::mat4(1.0f));
    }

    static Matrix4f Translation(float dx, float dy, float dz)
        noexcept(noexcept(glm::translate(std::declval<glm::mat4&>(), std::declval<glm::vec3>())))
    {
        glm::mat4 t(1.0f);
        t = glm::translate(t, glm::vec3(dx, dy, dz));
        return Matrix4f(t);
    }

    static Matrix4f Translation(const Vector3f& v)
        noexcept(noexcept(Translation(v.X(), v.Y(), v.Z())))
    {
        return Translation(v.X(), v.Y(), v.Z());
    }

    static Matrix4f Scaling(float sx, float sy, float sz)
        noexcept(noexcept(glm::scale(std::declval<glm::mat4&>(), std::declval<glm::vec3>())))
    {
        glm::mat4 s(1.0f);
        s = glm::scale(s, glm::vec3(sx, sy, sz));
        return Matrix4f(s);
    }

    static Matrix4f Scaling(const Vector3f& s)
        noexcept(noexcept(Scaling(s.X(), s.Y(), s.Z())))
    {
        return Scaling(s.X(), s.Y(), s.Z());
    }

    static Matrix4f Rotation(float angleDeg, float x, float y, float z)
        noexcept(noexcept(glm::rotate(std::declval<glm::mat4&>(), std::declval<float>(), std::declval<glm::vec3>())))
    {
        return Matrix4f(glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(x, y, z)));
    }

    static Matrix4f Rotation(Matrix4f& r, float angleDeg, float x, float y, float z)
        noexcept(noexcept(glm::rotate(std::declval<glm::mat4&>(), std::declval<float>(), std::declval<glm::vec3>())))
    {
        r = Matrix4f(glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(x, y, z)));
        return r;
    }

    static Matrix4f Rotation(float angleDeg, const Vector3f& axis)
        noexcept(noexcept(Rotation(angleDeg, axis.X(), axis.Y(), axis.Z())))
    {
        return Rotation(angleDeg, axis.X(), axis.Y(), axis.Z());
    }

    static Matrix4f& Rotation(Matrix4f& rotation, float x, float y, float z)
        noexcept(noexcept(rotation.EulerComputeZYX(0, 0, 0, 0, 0, 0)))
    {
        float radX = Conversions::DegToRad(x);
        float radY = Conversions::DegToRad(y);
        float radZ = Conversions::DegToRad(z);
        return rotation.EulerComputeZYX(sin(radX), cos(radX), sin(radY), cos(radY), sin(radZ), cos(radZ));
    }

    static Matrix4f Rotation(float x, float y, float z)
        noexcept(noexcept(Rotation(std::declval<Matrix4f&>(), x, y, z)))
    {
        Matrix4f rotation;
        return Rotation(rotation, x, y, z);
    }

    static Matrix4f Rotation(Matrix4f& rotation, Vector3f angles)
        noexcept(noexcept(Rotation(rotation, angles.X(), angles.Y(), angles.Z())))
    {
        return Rotation(rotation, angles.X(), angles.Y(), angles.Z());
    }

    static Matrix4f Rotation(Vector3f angles)
        noexcept(noexcept(Rotation(std::declval<Matrix4f&>(), angles.X(), angles.Y(), angles.Z())))
    {
        Matrix4f rotation;
        return Rotation(rotation, angles.X(), angles.Y(), angles.Z());
    }

    // ===== Member transforms =====
    Matrix4f& Translate(float x, float y, float z)
        noexcept(noexcept(Translation(x, y, z)))
    {
        m *= Translation(x, y, z).m;
        return *this;
    }

    Matrix4f& Translate(const Vector3f& v)
        noexcept(noexcept(Translate(v.X(), v.Y(), v.Z())))
    {
        return Translate(v.X(), v.Y(), v.Z());
    }

    Matrix4f& Scale(float sx, float sy, float sz)
        noexcept(noexcept(glm::scale(std::declval<glm::mat4&>(), std::declval<glm::vec3>())))
    {
        m = glm::scale(m, glm::vec3(sx, sy, sz));
        return *this;
    }

    Matrix4f& Scale(const glm::vec3& s)
        noexcept(noexcept(glm::scale(std::declval<glm::mat4&>(), s)))
    {
        m = glm::scale(m, s);
        return *this;
    }

    Matrix4f& Rotate(float angleDeg, const glm::vec3& axis)
        noexcept(noexcept(glm::rotate(std::declval<glm::mat4&>(), std::declval<float>(), axis)))
    {
        if ((angleDeg != 0.0f) and (glm::length(axis) != 0.0f))
            m = glm::rotate(m, glm::radians(angleDeg), axis);
        return *this;
    }

    Matrix4f& Rotate(float angleDeg, float x, float y, float z)
        noexcept(noexcept(Rotate(angleDeg, glm::vec3(x, y, z))))
    {
        return Rotate(angleDeg, glm::vec3(x, y, z));
    }

    template<typename T>  
        requires std::same_as<std::decay_t<T>, Matrix4f>
    Matrix4f& Rotate(T&& r) noexcept {
        m *= std::forward<T>(r).m;
        return *this;
    }

    // ===== LinAlg =====
    Matrix4f Transpose() const
        noexcept(noexcept(glm::transpose(std::declval<glm::mat4>())))
    {
        return Matrix4f(glm::transpose(m));
    }

    Matrix4f Transpose(Matrix4f& m, int /*dimensions*/ = 4) const
        noexcept(noexcept(glm::transpose(std::declval<glm::mat4>())))
    {
        return m = m.Transpose();
        m;
    }

    Matrix4f Inverse() const
        noexcept(noexcept(glm::inverse(std::declval<glm::mat4>())))
    {
        return Matrix4f(glm::inverse(m));
    }

    Matrix4f AffineInverse(void); // (lass offen – je nach Implementierung)

    static Vector3f Rotate(const Matrix4f& mm, const Vector3f& v)
        noexcept(noexcept(mm* v))
    {
        return mm * v;
    }

    template <typename T> requires std::same_as<std::decay_t<T>, Vector3f>
    Vector3f Rotate(T&& v) const
        noexcept(noexcept((*this)* std::forward<T>(v)))
    {
        return *this * std::forward<T>(v);
    }

    Vector3f Unrotate(const Vector3f v)
        noexcept(noexcept(Transpose()* v))
    {
        return Transpose() * v;
    }

    float Det() const
        noexcept(noexcept(glm::determinant(std::declval<glm::mat4>())))
    {
        return glm::determinant(m);
    }

    // ===== Operators =====
    Matrix4f operator*(const Matrix4f& other) const
        noexcept(noexcept(std::declval<glm::mat4>()* other.m))
    {
        return Matrix4f(m * other.m);
    }

    Matrix4f operator*(const glm::mat4& other) const
        noexcept(noexcept(std::declval<glm::mat4>()* other))
    {
        return Matrix4f(m * other);
    }

    Matrix4f& operator*=(const Matrix4f& other)
        noexcept(noexcept(std::declval<glm::mat4&>() *= other.m))
    {
        m *= other.m; 
        return *this;
    }

    Matrix4f& operator*=(const glm::mat4& other)
        noexcept(noexcept(std::declval<glm::mat4&>() *= other))
    {
        m *= other; 
        return *this;
    }

    Vector4f operator*(const Vector4f& v) const
        noexcept(noexcept(std::declval<glm::mat4>()* v))
    {
        return m * v;
    }

    Vector3f operator*(const Vector3f& v) const
        noexcept(noexcept(std::declval<glm::mat4>()* Vector4f(v)))
    {
        Vector4f h = v;
        return m * h;
    }

    operator const float* () const noexcept { return glm::value_ptr(m); }

    // ===== Utilities =====
    glm::vec4& R() noexcept { return m[0]; }
    glm::vec4& U() noexcept { return m[1]; }
    glm::vec4& F() noexcept { return m[2]; }
    glm::vec4& T() noexcept { return m[3]; }

    const glm::vec4& R() const noexcept { return m[0]; }
    const glm::vec4& U() const noexcept { return m[1]; }
    const glm::vec4& F() const noexcept { return m[2]; }
    const glm::vec4& T() const noexcept { return m[3]; }

    bool IsColMajor() const noexcept { return true; }

    bool IsValid() const noexcept {
        const float* arr = glm::value_ptr(m);
        for (int i = 0; i < 16; ++i)
            if (arr[i] != arr[i]) // NaN check
                return false;
        return true;
    }
};

// =================================================================================================
