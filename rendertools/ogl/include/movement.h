#pragma once

#include "vector.hpp"

// =================================================================================================

class Movement {
public:
    Vector3f    velocity;
    Vector3f    scale;
    float       length;
    Vector3f    normal;

    typedef enum {
        mtIntended,
        mtActual,
        mtRemaining,
        mtTotal,
        ntStep,
        mtCount
    } eMovementTypes;

    Movement()
        : velocity(Vector3f::ZERO), scale(Vector3f::ONE), length(0.0f), normal(Vector3f::ZERO)
    {
    }

    Movement(Vector3f v, Vector3f scale = Vector3f::ONE)
    {
        SetScale (scale);
        Update(v);
        Refresh();
    }

    inline void SetScale(Vector3f _scale) {
        scale = _scale;
    }

    Movement& Copy(const Movement& m) {
        velocity = m.velocity;
        scale = m.scale;
        length = m.length;
        normal = m.normal;
        return *this;
    }

    Movement& Refresh(float l = -1.0f) {
        length = (l < 0) ? velocity.Length() : l;
        normal = (length > Conversions::NumericTolerance) ? velocity / length : Vector3f::ZERO;
        return *this;
    }

    template <typename T>
    inline Movement& Update(T&& v, float l = -1.0f) {
        velocity = v * scale;
        return Refresh(l);
    }

    inline void Reset(void) {
        velocity = Vector3f::ZERO;
        normal = Vector3f::ZERO;
        length = 0.0f;
    }

    inline Movement& operator=(const Vector3f& v) {
        return this->Update(v);
    }

    inline Movement& operator=(const Movement& m) {
        return this->Copy(m);
    }

    inline Movement operator*(const float n) {
        Movement m(velocity * n, scale);
        return m;
    }

    inline Movement operator/(const float n) {
        Movement m(velocity / n, scale);
        return m;
    }

    inline Movement& operator*=(const float n) {
        velocity *= n;
        length *= n;
        return *this;
    }

    inline Movement& operator/=(const float n) {
        velocity /= n;
        length /= n;
        return *this;
    }

    inline Movement& operator+=(const Movement& m) {
        velocity += m.velocity;
        return Refresh();
    }

    inline Movement& operator-=(const Movement& m) {
        velocity -= m.velocity;
        return Refresh();
    }

    inline operator Vector3f() const { return velocity; }

    inline operator const Vector3f& () const { return velocity; }
};

// =================================================================================================
