#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>

template <typename VEC_TYPE>
class Vector : public VEC_TYPE {
public:
    using VEC_TYPE::VEC_TYPE; // Alle Konstruktoren übernehmen

    Vector() : VEC_TYPE(0) {}

    Vector(const VEC_TYPE& v) : VEC_TYPE(v) {}

    // Copy-Konstruktor aus anderem Vector
    template <typename OTHER_VEC>
    Vector(const Vector<OTHER_VEC>& other) {
        Assign(other);
    }

    Vector(std::initializer_list<float> list) {
        *this = list;
    }

    template <typename OTHER_VEC>
    Vector& operator=(const Vector<OTHER_VEC>& other) {
        Assign(other);
        return *this;
    }

    template <typename OTHER_VEC>
    Vector& operator=(const OTHER_VEC& other) {
        Assign(other);
        return *this;
    }

    inline float* Data() { return &((*this)[0]); }

    inline const float* Data() const { return static_cast<const float*>(&((*this)[0])); }

    inline int DataSize() const { return sizeof(float) * VEC_TYPE::length(); }

    inline Vector& operator+=(const Vector& other) {
        *this = static_cast<VEC_TYPE>(*this) + static_cast<VEC_TYPE>(other);
        return *this;
    }

    Vector& operator=(std::initializer_list<float> list) {
        int i = 0;
        for (auto it = list.begin(); it != list.end() and i < VEC_TYPE::length(); ++it)
            (*this)[i++] = *it;
        for (; i < VEC_TYPE::length(); ++i)
            (*this)[i] = 0.0f;
        return *this;
    }

    inline Vector& operator-=(const Vector& other) {
        *this = static_cast<VEC_TYPE>(*this) - static_cast<VEC_TYPE>(other);
        return *this;
    }

    inline Vector& operator*=(float scalar) {
        *this = static_cast<VEC_TYPE>(*this) * scalar;
        return *this;
    }

    inline Vector& operator/=(float scalar) {
        *this = static_cast<VEC_TYPE>(*this) / scalar;
        return *this;
    }

    inline Vector operator*(float scalar) const {
        return Vector(static_cast<VEC_TYPE>(*this) * scalar);
    }

    Vector operator*(const Vector& other) const {
        Vector v(*this);
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            v[i] *= other[i];
        return v;
    }

    Vector operator/(float scalar) const {
        return Vector(static_cast<VEC_TYPE>(*this) / scalar);
    }

    Vector operator/(const Vector& other) const {
        Vector v(*this);
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            v[i] /= other[i];
        return v;
    }

    Vector operator+(const Vector& other) const {
        return Vector(static_cast<VEC_TYPE>(*this) + static_cast<VEC_TYPE>(other));
    }

    Vector operator-(const Vector& other) const {
        return Vector(static_cast<VEC_TYPE>(*this) - static_cast<VEC_TYPE>(other));
    }

    Vector operator-() const {
        return Vector(-static_cast<VEC_TYPE>(*this));
    }

    Vector& Negate() {
        *this = -static_cast<VEC_TYPE>(*this);
        return *this;
    }

    bool operator==(const Vector& other) const {
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            if ((*this)[i] != other[i])
                return false;
        return true;
    }

    bool operator!=(const Vector& other) const {
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            if ((*this)[i] == other[i])
                return false;
        return true;
    }

    float Dot(const Vector& other, int range = VEC_TYPE::length()) const {
        float dot = 0.0f;
        for (int i = 0; i < range; ++i)
            dot += (*this)[i] * other[i];
        return dot;
    }

    float Dot(const float* other, int range = VEC_TYPE::length()) const {
        float dot = 0.0f;
        for (int i = 0; i < range; ++i)
            dot += (*this)[i] * other[i];
        return dot;
    }

    Vector Cross(const Vector& other) const {
        if constexpr (std::is_same_v<VEC_TYPE, glm::vec3>) {
            return Vector(glm::cross(*this, other));
        } else if constexpr (std::is_same_v<VEC_TYPE, glm::vec4>) {
            glm::vec3 a3(this->x, this->y, this->z);
            glm::vec3 b3(other.x, other.y, other.z);
            glm::vec3 c = glm::cross(a3, b3);
            return Vector(glm::vec4(c, 0.0f));
        } else {
            static_assert(std::is_same_v<VEC_TYPE, glm::vec3> or std::is_same_v<VEC_TYPE, glm::vec4>, "Cross nur für vec3 und vec4 definiert");
        }
    }

    inline float Length() const {
        return glm::length(static_cast<VEC_TYPE>(*this));
    }

    inline float LengthSquared() const {
        return glm::dot(static_cast<VEC_TYPE>(*this), static_cast<VEC_TYPE>(*this));
    }

    inline Vector& Normalize() {
        *this = glm::normalize(static_cast<VEC_TYPE>(*this));
        return *this;
    }

    inline Vector Normal() const {
        return Vector(glm::normalize(static_cast<VEC_TYPE>(*this)));
    }

    float Min() const {
        float v = (*this)[0];
        for (int i = 1; i < VEC_TYPE::length(); ++i)
            v = std::min(v, (*this)[i]);
        return v;
    }

    float Max() const {
        float v = (*this)[0];
        for (int i = 1; i < VEC_TYPE::length(); ++i)
            v = std::max(v, (*this)[i]);
        return v;
    }

    const Vector& Minimize(const Vector& other) {
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            if ((*this)[i] > other[i])
                (*this)[i] = other[i];
        return *this;
    }

    const Vector& Maximize(const Vector& other) {
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            if ((*this)[i] < other[i])
                (*this)[i] = other[i];
        return *this;
    }

    bool IsValid() const {
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            if (!std::isfinite((*this)[i]))
                return false;
        return true;
    }

    static Vector Perp(const Vector& v0, const Vector& v1, const Vector& v2) {
        return (v1 - v0).Cross(v2 - v0);
    }

    static Vector Normal(const Vector& v0, const Vector& v1, const Vector& v2) {
        return Perp(v0, v1, v2).Normalize();
    }

    inline Vector Reflect(const Vector& other) const { // reflect other at *this; *this must be normalized!
        return other - (*this * 2.0f * this->Dot(other));
    }

    inline Vector Abs(void) {
        Vector v;
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            v[i] = fabs(*this[i]);
        return v;
    }

    static inline Vector& Abs(Vector& v) {
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            v[i] = fabs(v[i]);
        return v;
    }

    static inline Vector& Abs(Vector&& v) {
        return Abs(static_cast<Vector&>(v));
    }

    static int Compare(Vector& v0, Vector& v1) {
        for (int i = 0; i < VEC_TYPE::length(); ++i) {
            if (v0[i] < v1[i])
                return -1;
            if (v0[i] > v1[i])
                return 1;
        }
        return 0;
    }

    operator const float* () const {
        return glm::value_ptr(static_cast<VEC_TYPE const&>(*this));
    }

    float* AsArray() {
        return const_cast<float*>(
            glm::value_ptr(static_cast<VEC_TYPE const&>(*this))
            );
    }

    static float Dot(float* v1, float* v2, int count) {
        float dot = 0.0f;
        for (int i = 0; i < count; ++i)
            dot += v1[i] * v2[i];
        return dot;
    }

    inline float X() const { return (*this)[0]; }
    inline float Y() const { return (*this)[1]; }
    inline float Z() const { return (VEC_TYPE::length() >= 3) ? (*this)[2] : 0.0f; }
    inline float W() const { return (VEC_TYPE::length() >= 4) ? (*this)[3] : 0.0f; }

    inline float R() const { return X(); }
    inline float G() const { return Y(); }
    inline float B() const { return Z(); }
    inline float A() const { return W(); }

    inline float U() const { return X(); }
    inline float V() const { return Y(); }

    inline float& X() { return (*this)[0]; }
    inline float& Y() { return (*this)[1]; }

    inline float& Z() {
        static_assert(VEC_TYPE::length() >= 3, "Z() nur für Vektoren mit >=3 Komponenten.");
        return (*this)[2];
    }

    inline float& W() {
        static_assert(VEC_TYPE::length() >= 4, "W() nur für Vektoren mit >=4 Komponenten.");
        return (*this)[3];
    }

    inline float& R() { return X(); }
    inline float& G() { return Y(); }
    inline float& B() { return Z(); }
    inline float& A() { return W(); }

    inline float& U() { return X(); }
    inline float& V() { return Y(); }

    static const Vector<VEC_TYPE> NONE;
    static const Vector<VEC_TYPE> ZERO;
    static const Vector<VEC_TYPE> ONE;

private:
    template <typename OTHER_VEC>
    void Assign(const OTHER_VEC& other) {
        this->x = other.x;
        this->y = other.y;

        if constexpr (VEC_TYPE::length() > 2) {
            if constexpr (OTHER_VEC::length() > 2)
                this->z = other.z;
            else
                this->z = 0.0f;
        }

        if constexpr (VEC_TYPE::length() > 3) {
            if constexpr (OTHER_VEC::length() > 3)
                this->w = other.w;
            else
                this->w = 0.0f;
        }
    }

};

using Vector2f = Vector<glm::vec2>;
using Vector3f = Vector<glm::vec3>;
using Vector4f = Vector<glm::vec4>;
