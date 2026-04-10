#pragma once

#include "conversions.hpp"

// =================================================================================================
// Texture coordinate representation

#if 1

#include "vector.hpp"

class TexCoord
    : public Vector2f
{
public:
    using Vector2f::Vector2f;

    const float Aspect(void) const noexcept {
        return (Y() > Conversions::NumericTolerance) ? X() / Y() : 1.0f;
    }
};

#else

class TexCoord {
    public:
        float   U(), V();

        TexCoord(float u = 0.0f, float v = 0.0f) : U()(u), V()(v) {}

        TexCoord& operator= (TexCoord const& other) {
            U() = other.U();
            V() = other.V();
            return *this;
        }
 
        TexCoord operator+ (TexCoord const& other) {
            return TexCoord(U() + U(), V() + V());
        }

        TexCoord operator- (TexCoord const& other) {
            return TexCoord(U() - U(), V() - V());
        }

        TexCoord& operator+= (TexCoord const& other) {
            U() += other.U();
            V() += other.V();
            return *this;
        }

        TexCoord& operator-= (TexCoord const& other) {
            U() -= other.U();
            V() -= other.V();
            return *this;
        }

        TexCoord& operator-() {
            U() = -U();
            V() = -V();
            return *this;
        }

        TexCoord& operator* (int n) {
            U() *= n;
            V() *= n;
            return *this;
        }
		
        const float Aspect(void) const noexcept {
            return (Y() > Conversions::NumericTolerance) ? X() / Y() : 1.0f;
        }
};

#endif

// =================================================================================================
