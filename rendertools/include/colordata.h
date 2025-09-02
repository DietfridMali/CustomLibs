#pragma once

#include "vector.hpp"

class RGBAColor;

class RGBColor : public Vector3f {
public:
    RGBColor(float r = 1.0f, float g = 1.0f, float b = 1.0f)
        : Vector3f({ r, g, b })
    {
    }

    RGBColor(const RGBColor& v)
        : Vector3f(v)
    {
    }

    explicit RGBColor(const Vector3f& v)
        : Vector3f(v)
    {
    }

    explicit operator RGBAColor() const;
};


class RGBAColor : public Vector4f {
public:
    RGBAColor(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f)
        : Vector4f({ r, g, b, a })
    {
    }

    RGBAColor(const Vector4f& v)
        : Vector4f(v)
    {
    }

    RGBAColor(const RGBColor& rgb, float alpha = 1.0f)
        : Vector4f({ rgb.R(), rgb.G(), rgb.B(), alpha })
    {
    }

    RGBAColor& operator=(const RGBColor& rgb) {
        this->R() = rgb.R();
        this->G() = rgb.G();
        this->B() = rgb.B();
        this->A() = 1.0f;
        return *this;
    }

    explicit operator RGBColor() const {
        return RGBColor(this->X(), this->Y(), this->Z());
    }

    inline bool IsVisible(void) {
        return A() > 0.0f;
    }

    RGBAColor Premultiplied(void) const {
        return (A() < 1.0f) ? RGBAColor (R() * A(), G() * A(), B() * A(), A()) : *this;
    }

    static RGBAColor& Premultiply(RGBAColor& color) {
        if (color.A() < 1.0f) {
            color.R() *= color.A();
            color.G() *= color.A();
            color.B() *= color.A();
        }
    }
};


// Definition nach RGBAColor-Klasse
inline RGBColor::operator RGBAColor() const {
    return RGBAColor(*this);
}

class ColorData {
public:
    inline static const RGBAColor   Invisible = RGBAColor{ 0, 0, 0, 0 };
    inline static const RGBAColor   Black = RGBAColor{ 0, 0, 0, 1 };
    inline static const RGBAColor   White = RGBAColor{ 1, 1, 1, 1 };
    inline static const RGBAColor   Gray = RGBAColor{ 0.5f, 0.5f, 0.5f, 1 };
    inline static const RGBAColor   LightGray = RGBAColor{ 0.75f, 0.75f, 0.75f, 1 };
    inline static const RGBAColor   DarkGray = RGBAColor{ 0.25f, 0.25f, 0.25f, 1 };
    inline static const RGBAColor   Gold = RGBAColor{ 1.0f, 0.8f, 0.0f, 1 };
    inline static const RGBAColor   Yellow = RGBAColor{ 1.0f, 1.0f, 0.0f, 1 };
    inline static const RGBAColor   Orange = RGBAColor{1.0f, 0.5f, 0.0f, 1 };
    inline static const RGBAColor   Red = RGBAColor{ 0.8f, 0.0f, 0.0f, 1 };
    inline static const RGBAColor   Green = RGBAColor{ 0.0f, 0.8f, 0.0f, 1 };
    inline static const RGBAColor   Blue = RGBAColor{ 0.0f, 0.0f, 0.8f, 1 };
    inline static const RGBAColor   LightBlue = RGBAColor{ 0.0f, 0.8f, 1.0f, 1 };
    inline static const RGBAColor   MediumBlue = RGBAColor{ 0.0f, 0.5f, 1.0f, 1 };
    inline static const RGBAColor   MediumGreen = RGBAColor{ 0.0f, 1.0f, 0.5f, 1 };
    inline static const RGBAColor   LightGreen = RGBAColor{ 0.0f, 1.0f, 0.8f, 1 };
    inline static const RGBAColor   Magenta = RGBAColor{ 1.0f, 0.0f, 1.0f, 1 };
    inline static const RGBAColor   Purple = RGBAColor{ 0.5f, 0.0f, 0.5f, 1 };
    inline static const RGBAColor   Brown = RGBAColor{ 0.45f, 0.25f, 0.1f, 1 };
};
