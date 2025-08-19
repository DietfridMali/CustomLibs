#pragma once

// =================================================================================================
// Texture coordinate representation

#if 1

#include "vector.hpp"

using TexCoord = Vector2f;

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
		
};

#endif

// =================================================================================================
