#pragma once

#include "vector.hpp"
#include "conversions.hpp"

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

    RGBAColor Mix(const RGBAColor& other, float t) const noexcept {
        t = std::clamp(t, 0.0f, 1.0f);
        return RGBAColor(std::lerp(R(), other.R(), t), std::lerp(G(), other.G(), t), std::lerp(B(), other.B(), t), std::lerp(A(), other.A(), t));
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

    float GrayValue(bool perceptive = true) const noexcept {
        static Vector3f luminance[2]{ { 0.299f, 0.587f, 0.114f }, { 0.2126f,0.7152f,0.0722f } };
        return Vector3f(*this).Dot(luminance[perceptive]);
    }

    inline float Average(void) noexcept {
        return (R() + B() + G()) / 3.0f;
    }


    // Liefert die MULTIPLIKATIVE Skalen-"Farbe" (RGB um 1.0 herum),
// die aus der Tint-Grundfarbe (*this) berechnet wird.
// strength ∈ [0,1], keepLuminance = L(scale) auf 1 normieren.
    RGBAColor Tint(float strength = 1.0f, bool perceptive = true) const noexcept  {
        float pivot = GrayValue(perceptive);
        if (pivot < Conversions::NumericTolerance)
            return *this;
        RGBColor scale = RGBColor(*this);
        scale -= RGBColor(pivot, pivot, pivot);
        scale *= std::clamp(strength, 0.0f, 1.0f);
        scale += Vector3f::ONE;                              // um 1.0 herum -> reine Multiplikation
        return RGBAColor(scale.R(), scale.G(), scale.B(), 1.0f);
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
    inline static const RGBAColor   Green = RGBAColor{ 0.0f, 0.8f, 0.2f, 1 };
    inline static const RGBAColor   DarkGreen = RGBAColor{ 0.0f, 0.4f, 0.1f, 1 };
    inline static const RGBAColor   Blue = RGBAColor{ 0.0f, 0.2f, 0.8f, 1 };
    inline static const RGBAColor   LightBlue = RGBAColor{ 0.0f, 0.8f, 1.0f, 1 };
    inline static const RGBAColor   MediumBlue = RGBAColor{ 0.0f, 0.5f, 1.0f, 1 };
    inline static const RGBAColor   MediumGreen = RGBAColor{ 0.0f, 1.0f, 0.5f, 1 };
    inline static const RGBAColor   LightGreen = RGBAColor{ 0.0f, 1.0f, 0.8f, 1 };
    inline static const RGBAColor   Magenta = RGBAColor{ 1.0f, 0.0f, 1.0f, 1 };
    inline static const RGBAColor   Purple = RGBAColor{ 0.5f, 0.0f, 0.5f, 1 };
    inline static const RGBAColor   Brown = RGBAColor{ 0.45f, 0.25f, 0.1f, 1 };
};
