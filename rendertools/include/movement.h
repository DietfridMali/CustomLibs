#pragma once

#include "vector.hpp"

// =================================================================================================

class Movement {
public:
    Vector3f    direction;
    Vector3f    scale;
    float       length;
    Vector3f    normal;

    typedef enum {
        mtIntended,
        mtActual,
        mtRemaining,
        mtTotal,
        mtCount
    } eMovementTypes;

    Movement()
        : direction(Vector3f::ZERO), scale(Vector3f::ONE), length(0.0f), normal(Vector3f::ZERO)
    {
    }

    Movement(Vector3f v, Vector3f scale = Vector3f::ONE)
    {
        SetScale (scale);
        Update(v);
        Refresh();
    }

    inline void SetScale(Vector3f scale) {
        this->scale = scale;
    }

    Movement& Copy(const Movement& m) {
        direction = m.direction;
        scale = m.scale;
        length = m.length;
        normal = m.normal;
        return *this;
    }

    Movement& Refresh(float l = -1.0f) {
        length = (l < 0) ? direction.Length() : l;
        normal = (length > Conversions::NumericTolerance) ? direction / length : Vector3f::ZERO;
        return *this;
    }

    template <typename T>
    inline Movement& Update(T&& v, float l = -1.0f) {
        direction = v * scale;
        return Refresh(l);
    }

    inline void Reset(void) {
        direction = Vector3f::ZERO;
        normal = Vector3f::ZERO;
        length = 0.0f;
    }

    inline Movement& operator=(const Vector3f& v) {
        return this->Update(v);
    }

    inline Movement& operator=(const Movement& m) {
        return this->Copy(m);
    }

    inline Movement operator*(const float scale) {
        Movement m(direction * scale);
        return m;
    }

    inline Movement operator/(const float scale) {
        Movement m(direction / scale);
        return m;
    }

    inline Movement& operator*=(const float scale) {
        direction *= scale;
        length *= scale;
        return *this;
    }

    inline Movement& operator/=(const float scale) {
        direction /= scale;
        length /= scale;
        return *this;
    }

    inline Movement& operator+=(const Movement& m) {
        direction += m.direction;
        return Refresh();
    }

    inline Movement& operator-=(const Movement& m) {
        direction -= m.direction;
        return Refresh();
    }

    inline operator Vector3f() const { return direction; }

    inline operator const Vector3f& () const { return direction; }
};

// =================================================================================================
