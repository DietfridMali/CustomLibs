#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>
#include <limits>

// =================================================================================================

template <typename VEC_TYPE>
class Vector : public VEC_TYPE {
public:
    using VEC_TYPE::VEC_TYPE; // übernimmt alle ctors

    Vector() noexcept(noexcept(VEC_TYPE(0))) 
        : VEC_TYPE(0) 
    { }

    Vector(const VEC_TYPE& v) noexcept : VEC_TYPE(v) 
    { }

    // Copy-Konstruktor aus anderem Vector
    template <typename OTHER_VEC>
    Vector(const Vector<OTHER_VEC>& other) noexcept { 
        Assign(other); 
    }

    Vector(std::initializer_list<float> list) noexcept { 
        *this = list; 
    }

    template <typename OTHER_VEC>
    Vector& operator=(const Vector<OTHER_VEC>& other) noexcept { 
        Assign(other); 
        return *this; 
    }

    template <typename OTHER_VEC>
    Vector& operator=(const OTHER_VEC& other) noexcept { 
        Assign(other); 
        return *this; 
    }

    inline float* Data() noexcept { 
        return &((*this)[0]); 
    }

    inline const float* Data() const noexcept { 
        return glm::value_ptr(static_cast<VEC_TYPE const&>(*this)); 
    }
    
    inline int DataSize() const noexcept { 
        return int(sizeof(float) * VEC_TYPE::length()); 
    }

    inline Vector& operator+=(const Vector& other)
        noexcept(noexcept(static_cast<VEC_TYPE&>(*this) = static_cast<VEC_TYPE>(*this) + static_cast<VEC_TYPE>(other)))
    {
        *this = static_cast<VEC_TYPE>(*this) + static_cast<VEC_TYPE>(other); 
        return *this;
    }

    Vector& operator=(std::initializer_list<float> list) noexcept {
        int i = 0;
        for (auto it = list.begin(); it != list.end() && i < VEC_TYPE::length(); ++it) 
            (*this)[i++] = *it;
        for (; i < VEC_TYPE::length(); ++i) 
            (*this)[i] = 0.0f;
        return *this;
    }

    inline Vector& operator-=(const Vector& other)
        noexcept(noexcept(static_cast<VEC_TYPE&>(*this) = static_cast<VEC_TYPE>(*this) - static_cast<VEC_TYPE>(other)))
    {
        *this = static_cast<VEC_TYPE>(*this) - static_cast<VEC_TYPE>(other); return *this;
    }

    inline Vector& operator*=(float scalar)
        noexcept(noexcept(static_cast<VEC_TYPE&>(*this) = static_cast<VEC_TYPE>(*this) * scalar))
    {
        *this = static_cast<VEC_TYPE>(*this) * scalar; 
        return *this;
    }

    inline Vector& operator/=(float scalar)
        noexcept(noexcept(static_cast<VEC_TYPE&>(*this) = static_cast<VEC_TYPE>(*this) / scalar))
    {
        *this = static_cast<VEC_TYPE>(*this) / scalar; 
        return *this;
    }

    inline Vector operator*(float scalar) const
        noexcept(noexcept(Vector(static_cast<VEC_TYPE>(*this)* scalar)))
    {
        return Vector(static_cast<VEC_TYPE>(*this) * scalar);
    }

    Vector operator*(const Vector& other) const noexcept {
        Vector v(*this);
        for (int i = 0; i < VEC_TYPE::length(); ++i) v[i] *= other[i];
        return v;
    }

    Vector& operator*=(const Vector& other) noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) (*this)[i] *= other[i];
        return *this;
    }

    inline Vector& operator*=(Vector&& other) noexcept { return (*this *= static_cast<Vector&>(other)); }

    Vector& operator/=(const Vector& other) noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) (*this)[i] /= other[i];
        return *this;
    }

    inline Vector& operator/=(Vector&& other) noexcept { return (*this /= static_cast<const Vector&>(other)); }

    Vector operator/(float scalar) const
        noexcept(noexcept(Vector(static_cast<VEC_TYPE>(*this) / scalar)))
    {
        return Vector(static_cast<VEC_TYPE>(*this) / scalar);
    }

    Vector operator/(const Vector& other) const noexcept {
        Vector v(*this);
        for (int i = 0; i < VEC_TYPE::length(); ++i) v[i] /= other[i];
        return v;
    }

    Vector operator+(const Vector& other) const
        noexcept(noexcept(Vector(static_cast<VEC_TYPE>(*this) + static_cast<VEC_TYPE>(other))))
    {
        return Vector(static_cast<VEC_TYPE>(*this) + static_cast<VEC_TYPE>(other));
    }

    Vector operator-(const Vector& other) const
        noexcept(noexcept(Vector(static_cast<VEC_TYPE>(*this) - static_cast<VEC_TYPE>(other))))
    {
        return Vector(static_cast<VEC_TYPE>(*this) - static_cast<VEC_TYPE>(other));
    }

    Vector operator-() const noexcept(noexcept(Vector(-static_cast<VEC_TYPE>(*this))))
    {
        return Vector(-static_cast<VEC_TYPE>(*this));
    }

    Vector& Negate() noexcept(noexcept(*this = -static_cast<VEC_TYPE>(*this)))
    {
        *this = -static_cast<VEC_TYPE>(*this); return *this;
    }

    bool operator==(const Vector& other) const noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) 
            if ((*this)[i] != other[i]) 
                return false;
        return true;
    }

    bool operator!=(const Vector& other) const noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) 
            if ((*this)[i] != other[i]) 
                return true;
        return false;
    }

    inline static Vector Floor(Vector v) {
        return Vector(glm::floor(v));
    }

    inline static Vector Ceil(Vector v) {
        return Vector(glm::ceil(v));
    }

    inline static Vector Round(Vector v) {
        return Vector(glm::round(v));
    }

    float Dot(const Vector& other, int range = VEC_TYPE::length()) const noexcept {
        float dot = 0.0f;
        for (int i = 0; i < range; ++i) 
            dot += (*this)[i] * other[i];
        return dot;
    }
    float Dot(const float* other, int range = VEC_TYPE::length()) const noexcept {
        float dot = 0.0f;
        for (int i = 0; i < range; ++i) 
            dot += (*this)[i] * other[i];
        return dot;
    }

    Vector Cross(const Vector& other) const
        noexcept(
            std::is_same_v<VEC_TYPE, glm::vec3> ?
            noexcept(glm::cross(std::declval<glm::vec3>(), std::declval<glm::vec3>()))
            : std::is_same_v<VEC_TYPE, glm::vec4> ?
            noexcept(glm::cross(std::declval<glm::vec3>(), std::declval<glm::vec3>()))
            : false
            )
    {
        if constexpr (std::is_same_v<VEC_TYPE, glm::vec3>) {
            // Falls dein Vector konvertierbar ist:
            // return Vector( glm::cross(static_cast<glm::vec3>(*this), static_cast<glm::vec3>(other)) );
            // oder einfach aus Komponenten bauen:
            glm::vec3 a3{ this->x, this->y, this->z };
            glm::vec3 b3{ other.x, other.y, other.z };
            return Vector(glm::cross(a3, b3));
        }
        else if constexpr (std::is_same_v<VEC_TYPE, glm::vec4>) {
            glm::vec3 a3{ this->x, this->y, this->z };
            glm::vec3 b3{ other.x, other.y, other.z };
            glm::vec3 c = glm::cross(a3, b3);
            return Vector(glm::vec4(c, 0.0f));
        }
        else {
            static_assert(std::is_same_v<VEC_TYPE, glm::vec3> || std::is_same_v<VEC_TYPE, glm::vec4>,
                "Cross only defined for vec3 and vec4");
        }
    }

    inline float Length() const
        noexcept(noexcept(glm::length(static_cast<VEC_TYPE>(*this))))
    {
        return glm::length(static_cast<VEC_TYPE>(*this));
    }

    inline float LengthSquared() const
        noexcept(noexcept(glm::dot(static_cast<VEC_TYPE>(*this), static_cast<VEC_TYPE>(*this))))
    {
        return glm::dot(static_cast<VEC_TYPE>(*this), static_cast<VEC_TYPE>(*this));
    }

    inline Vector& Normalize()
        noexcept(noexcept(static_cast<VEC_TYPE&>(*this) = glm::normalize(static_cast<VEC_TYPE>(*this))))
    {
        float l = LengthSquared();
        if ((l != 0.0f) and (l != 1.0f))
            *this /= sqrtf(l); // glm::normalize(static_cast<VEC_TYPE>(*this));
        return *this;
    }

    inline Vector Normal() const
        noexcept(noexcept(Vector(glm::normalize(static_cast<VEC_TYPE>(*this)))))
    {
        return Vector(glm::normalize(static_cast<VEC_TYPE>(*this)));
    }

    float Min() const noexcept {
        float v = (*this)[0];
        for (int i = 1; i < VEC_TYPE::length(); ++i) 
            v = std::min(v, (*this)[i]);
        return v;
    }
    float Max() const noexcept {
        float v = (*this)[0];
        for (int i = 1; i < VEC_TYPE::length(); ++i) 
            v = std::max(v, (*this)[i]);
        return v;
    }

    const Vector& Minimize(const Vector& other) noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) 
            if ((*this)[i] > other[i]) 
                (*this)[i] = other[i];
        return *this;
    }
    const Vector& Maximize(const Vector& other) noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) 
            if ((*this)[i] < other[i]) 
                (*this)[i] = other[i];
        return *this;
    }

    bool IsValid() const noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) 
            if (!std::isfinite((*this)[i])) 
                return false;
        return true;
    }

    bool IsZero() const noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i)
            if ((*this)[i])
                return false;
        return true;
    }

    static Vector Perp(const Vector& v0, const Vector& v1, const Vector& v2)
        noexcept(noexcept((v1 - v0).Cross(v2 - v0)))
    {
        return (v1 - v0).Cross(v2 - v0);
    }

    static Vector Normal(const Vector& v0, const Vector& v1, const Vector& v2)
        noexcept(noexcept(Perp(v0, v1, v2).Normalize()))
    {
        return Perp(v0, v1, v2).Normalize();
    }

    inline Vector Reflect(const Vector& other) const noexcept
    {
        return other - (*this * 2.0f * this->Dot(other));
    }

    template <typename T>
	inline float Distance(T&& other) const noexcept {
		return (*this - std::forward<T>(other)).Length();
    }

    inline Vector Abs(void) const noexcept {           // FIX: const + Indexierung
        Vector v;
        for (int i = 0; i < VEC_TYPE::length(); ++i) 
            v[i] = std::fabs((*this)[i]);
        return v;
    }

    static inline Vector& Abs(Vector& v) noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) v[i] = std::fabs(v[i]);
        return v;
    }
    static inline Vector& Abs(Vector&& v) noexcept { return Abs(static_cast<Vector&>(v)); }

    static int Compare(Vector& v0, Vector& v1) noexcept {
        for (int i = 0; i < VEC_TYPE::length(); ++i) {
            if (v0[i] < v1[i]) return -1;
            if (v0[i] > v1[i]) return 1;
        }
        return 0;
    }

    operator const float* () const noexcept {
        return glm::value_ptr(static_cast<VEC_TYPE const&>(*this));
    }

    float* AsArray() noexcept {
        return const_cast<float*>(glm::value_ptr(static_cast<VEC_TYPE const&>(*this)));
    }

    static float Dot(float* v1, float* v2, int count) noexcept {
        float dot = 0.0f;
        for (int i = 0; i < count; ++i) dot += v1[i] * v2[i];
        return dot;
    }

    inline float X() const noexcept { return (*this)[0]; }
    inline float Y() const noexcept { return (*this)[1]; }
    inline float Z() const noexcept { return (VEC_TYPE::length() >= 3) ? (*this)[2] : 0.0f; }
    inline float W() const noexcept { return (VEC_TYPE::length() >= 4) ? (*this)[3] : 0.0f; }

    inline float R() const noexcept { return X(); }
    inline float G() const noexcept { return Y(); }
    inline float B() const noexcept { return Z(); }
    inline float A() const noexcept { return W(); }

    inline float U() const noexcept { return X(); }
    inline float V() const noexcept { return Y(); }

    inline float& X() noexcept { return (*this)[0]; }
    inline float& Y() noexcept { return (*this)[1]; }

    inline float& Z() noexcept {
        static_assert(VEC_TYPE::length() >= 3, "Z() only for vektors with >= 3 components.");
        return (*this)[2];
    }
    inline float& W() noexcept {
        static_assert(VEC_TYPE::length() >= 4, "W() only for vektors with >= 4 components.");
        return (*this)[3];
    }

    inline float& R() noexcept { return X(); }
    inline float& G() noexcept { return Y(); }
    inline float& B() noexcept { return Z(); }
    inline float& A() noexcept { return W(); }

    inline float& U() noexcept { return X(); }
    inline float& V() noexcept { return Y(); }

    static const Vector<VEC_TYPE> NONE;
    static const Vector<VEC_TYPE> ZERO;
    static const Vector<VEC_TYPE> HALF;
    static const Vector<VEC_TYPE> ONE;

private:
    template <typename OTHER_VEC>
    void Assign(const OTHER_VEC& other) noexcept {
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

// =================================================================================================

template<typename VEC_TYPE>
inline const Vector<VEC_TYPE> Vector<VEC_TYPE>::ZERO(0.0f);

template<typename VEC_TYPE>
inline const Vector<VEC_TYPE> Vector<VEC_TYPE>::HALF(0.5f);

template<typename VEC_TYPE>
inline const Vector<VEC_TYPE> Vector<VEC_TYPE>::ONE(1.0f);

template<typename VEC_TYPE>
inline const Vector<VEC_TYPE> Vector<VEC_TYPE>::NONE(std::numeric_limits<float>::quiet_NaN());

// =================================================================================================

using Vector2f = Vector<glm::vec2>;
using Vector3f = Vector<glm::vec3>;
using Vector4f = Vector<glm::vec4>;

using Vector2i = glm::ivec2;
using Vector3i = glm::ivec3;
using Vector4i = glm::ivec4;

// =================================================================================================
