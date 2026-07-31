#pragma once

#include <cmath>
#include <initializer_list>

#include "vector.hpp"
#include "conversions.hpp"
#include "matrix.hpp"

// =================================================================================================
// An Euler angle triple in DEGREES, normalized to the half open interval [-180, +180).
//
// The component order is the one of the Vector classes: x = pitch, y = heading (yaw), z = bank
// (roll) - i.e. exactly what Matrix4f::Rotation () / EulerComputeYXZ () expect. The traditional
// Descent names are available as P (), H () and B ().
//
// Every constructor, assignment and arithmetic operator restores the normalization, so angle
// differences always come out as the short way round and "abs (delta) < threshold" tests work.
// Writing through the P ()/H ()/B () references bypasses that - call Wrap () afterwards.
//
// NOTE: angular *rates* (revolutions per second and the like) are NOT angles. They must not be
// wrapped and therefore stay plain Vector3f.
// =================================================================================================

class AngleVector : public Vector3f {
public:
    // ===== normalization =====
    static inline float Wrap(float a) noexcept {
        a = std::fmod(a + 180.0f, 360.0f);
        if (a < 0.0f)
            a += 360.0f;
        return a - 180.0f;
    }

    inline AngleVector& Wrap(void) noexcept {
        X() = Wrap(X());
        Y() = Wrap(Y());
        Z() = Wrap(Z());
        return *this;
    }

    // ===== Special Members =====
    AngleVector() noexcept : Vector3f(0.0f, 0.0f, 0.0f) {}

    AngleVector(float pitch, float heading, float bank) noexcept
        : Vector3f(pitch, heading, bank)
    {
        Wrap();
    }

    // no explicit exception specification here - let the compiler deduce it from the base class
    AngleVector(const AngleVector&) = default;
    AngleVector& operator=(const AngleVector&) = default;

    explicit AngleVector(const Vector3f& v) noexcept : Vector3f(v) { Wrap(); }

    AngleVector(std::initializer_list<float> list) noexcept {
        Vector3f::operator=(list);
        Wrap();
    }

    // ===== assignment =====
    // declaring these hides the base class operator= overloads, which is intended - every
    // assignment has to go through the normalization
    AngleVector& operator=(const Vector3f& v) noexcept {
        Vector3f::operator=(v);
        return Wrap();
    }

    AngleVector& operator=(std::initializer_list<float> list) noexcept {
        Vector3f::operator=(list);
        return Wrap();
    }

    // ===== component access =====
    inline float& P(void) noexcept { return X(); }      // pitch
    inline float& H(void) noexcept { return Y(); }      // heading (yaw)
    inline float& B(void) noexcept { return Z(); }      // bank (roll)

    inline float P(void) const noexcept { return X(); }
    inline float H(void) const noexcept { return Y(); }
    inline float B(void) const noexcept { return Z(); }

    // ===== degrees <-> radians =====
    // radians are NOT wrapped to [-180, +180), so they are handed out as a plain vector
    inline Vector3f Radians(void) const noexcept {
        return Vector3f(Conversions::DegToRad(X()), Conversions::DegToRad(Y()), Conversions::DegToRad(Z()));
    }

    static inline AngleVector FromRadians(const Vector3f& rad) noexcept {
        return AngleVector(Conversions::RadToDeg(rad.X()), Conversions::RadToDeg(rad.Y()), Conversions::RadToDeg(rad.Z()));
    }

    // ===== arithmetics =====
    // The base class operators return Vector3f and do not normalize, so the full set has to be
    // redeclared here - declaring one of them hides all base overloads of the same name anyway.
    inline AngleVector operator+(const AngleVector& other) const noexcept {
        return AngleVector(X() + other.X(), Y() + other.Y(), Z() + other.Z());
    }

    inline AngleVector operator-(const AngleVector& other) const noexcept {
        return AngleVector(X() - other.X(), Y() - other.Y(), Z() - other.Z());
    }

    inline AngleVector operator-(void) const noexcept {
        return AngleVector(-X(), -Y(), -Z());
    }

    inline AngleVector operator*(float scalar) const noexcept {
        return AngleVector(X() * scalar, Y() * scalar, Z() * scalar);
    }

    inline AngleVector operator/(float scalar) const noexcept {
        return AngleVector(X() / scalar, Y() / scalar, Z() / scalar);
    }

    inline AngleVector operator*(const Vector3f& other) const noexcept {
        return AngleVector(X() * other.X(), Y() * other.Y(), Z() * other.Z());
    }

    inline AngleVector operator/(const Vector3f& other) const noexcept {
        return AngleVector(X() / other.X(), Y() / other.Y(), Z() / other.Z());
    }

    inline AngleVector& operator+=(const AngleVector& other) noexcept {
        X() += other.X(); Y() += other.Y(); Z() += other.Z();
        return Wrap();
    }

    inline AngleVector& operator-=(const AngleVector& other) noexcept {
        X() -= other.X(); Y() -= other.Y(); Z() -= other.Z();
        return Wrap();
    }

    inline AngleVector& operator*=(float scalar) noexcept {
        X() *= scalar; Y() *= scalar; Z() *= scalar;
        return Wrap();
    }

    inline AngleVector& operator/=(float scalar) noexcept {
        X() /= scalar; Y() /= scalar; Z() /= scalar;
        return Wrap();
    }

    inline AngleVector& operator*=(const Vector3f& other) noexcept {
        X() *= other.X(); Y() *= other.Y(); Z() *= other.Z();
        return Wrap();
    }

    inline AngleVector& operator/=(const Vector3f& other) noexcept {
        X() /= other.X(); Y() /= other.Y(); Z() /= other.Z();
        return Wrap();
    }

    static const AngleVector ZERO;
};

// =================================================================================================

inline const AngleVector AngleVector::ZERO;

// =================================================================================================
// Matrix4f interoperation. The declarations live in glm_matrix.hpp so that the matrix header does
// not have to know this one; they are defined here because they need the complete type.

#if USE_GLM

inline Matrix4f::Matrix4f(const AngleVector& angles, bool transpose) noexcept {
    Rotation(*this, angles.P(), angles.H(), angles.B(), transpose);
}

inline Matrix4f& Matrix4f::operator=(const AngleVector& angles) noexcept {
    return Rotation(*this, angles.P(), angles.H(), angles.B(), false);
}

inline Matrix4f& Matrix4f::Rotation(Matrix4f& rotation, const AngleVector& angles, bool transpose) noexcept {
    return Rotation(rotation, angles.P(), angles.H(), angles.B(), transpose);
}

inline Matrix4f Matrix4f::Rotation(const AngleVector& angles, bool transpose) noexcept {
    Matrix4f rotation;
    return Rotation(rotation, angles.P(), angles.H(), angles.B(), transpose);
}

#endif // USE_GLM

// =================================================================================================
