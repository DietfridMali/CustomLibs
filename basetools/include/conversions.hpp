#pragma once

#define _USE_MATH_DEFINES // for C++

#include <concepts>   // std::integral, std::signed_integral
#include <utility>    // std::pair
#include <stdexcept>  // std::invalid_argument
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <memory>
#include <new>    // std::nothrow

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f; 

// =================================================================================================

namespace Conversions
{
    static float DegToRad(float deg)
        noexcept
    {
        return static_cast<float>((static_cast<double>(deg) / 180.0 * PI));
    }

    static float RadToDeg(float rad)
        noexcept
    {
        return static_cast<float>((static_cast<double>(rad) * 180.0 / PI));
    }

    static float DotToRad(float dot)
        noexcept
    {
        return static_cast<float>(acos(dot));
    }

    static float DotToDeg(float dot)
        noexcept
    {
        return RadToDeg(DotToRad(dot));
    }

    static float Normalize(float v, float vMin, float vMax) {
        return (v - vMin) / (vMax - vMin);
    }

    // pulls everything below the middle towards the min value, everything above the middle towards the max value
    static float Stretch(float v, float vMin, float vMax) {
        float m = (vMax - vMin) * 0.5f;
        float h = vMin + m;
        return (v < m) ? h * (m - v) / (m - vMin) : (1.0f - h) * (v - m) / (vMax - m);
    }

    template <typename T, size_t N>
    constexpr size_t ArrayLength(const T(&)[N]) noexcept { return N; }

    template<typename T>
    constexpr int Sign(T val)
        noexcept
    {
        return (T(0) < val) - (val < T(0));
    }

    constexpr float NumericTolerance = 0.000001f;

    template<typename T>
    struct Interval {
        T min = std::numeric_limits<T>::lowest();
        T max = std::numeric_limits<T>::max();
        bool Contains(T v) const
            noexcept
        {
            return v >= this->min and v <= this->max;
        }
    };

    using FloatInterval = Interval<float>;


    // Liefert floor(sqrt(i)) für beliebige Integraltypen (signed/unsigned)
    // Newton-Raphson algorithm
    template <std::integral Int>
    constexpr std::make_unsigned_t<Int> IntSqrt(Int i) {
        using Unsign = std::make_unsigned_t<Int>;

        if constexpr (std::signed_integral<Int>) {
            if (i < 0) throw std::domain_error("isqrt_fast: negativer Eingangswert");
        }

        Unsign n = static_cast<Unsign>(i);
        if (n < 2) return n;

        // Startwert: 2^ceil(bit_width(n)/2)  (leichtes Über-Estimate)
        unsigned bw = std::bit_width(n);
        Unsign x = Unsign{ 1 } << ((bw + 1) / 2);

        // Integer-Newton: x_{k+1} = floor((x + n/x) / 2)
        // Monoton fallend bis zum Fixpunkt; Division durch 0 ausgeschlossen (x>0).
        while (true) {
            Unsign y = (x + n / x) >> 1;
            if (y >= x)
                break;  // konvergiert: y == x oder y < x nicht mehr
            x = y;
        }

        // Exakte Korrektur (max. 1–2 Schritte), geschützt vor Overflow via Division
        while (x > n / x)
            --x;
        while ((x + 1) <= n / (x + 1))
            ++x;

        return x; // floor(sqrt(n))
    }


    template <std::integral Int>
    struct IntDimensions {
        Int     width{ 0 };
        Int     height{ 0 };
    };


    // compute biggest integer a <= sqrt(n) so that n / d is also an integer value
    // in other words: 2D dimension {a, b} of a number, so that a and b are as close together as possible
    template <std::integral Int>
    IntDimensions<Int> NearestDivisors(Int n)
        noexcept
    {
        if (n == 0)
            return { 0, 0 };
        //throw std::invalid_argument("n=0 hat unendlich viele Teiler.");

        using Unsign = std::make_unsigned_t<Int>;
        // sicherer Absolutbetrag ohne Overflow bei INT_MIN:
        Unsign m = (n < 0) ? Unsign(-(n + 1)) + 1 : Unsign(n);

        Unsign d = IntSqrt(m);               // unsere schnelle integer sqrt
        while ((d > 0) and (m % d != 0)) {   // Schritt zurück, bis echter Teiler
            --d;
        }

        Int w = static_cast<Int>(d);
        if constexpr (std::signed_integral<Int>) {
            if (n < 0)
                w = -w;           // bei negativen n a negativ wählen
        }

        return { w, n / w };
    }


    template <typename T>
    T OptionalValue(std::optional<T> var, T defVal) {
        return var.has_value() ? var.value() : defVal;
    }


    inline float Rampi(int64_t value, int64_t minValue, int64_t maxValue, int64_t threshold) noexcept {
        if (threshold < 1)
            return 0.0f;
        if ((value < minValue) or (value > maxValue))
            return 0.0f;
        int64_t d = std::min(value - minValue, maxValue - value);
        return (d >= threshold) ? 1.0f : float (d) / float(threshold);
    }


    inline float Rampf(float value, float minValue, float maxValue, float threshold) noexcept {
        if (threshold < NumericTolerance)
            return 0.0f;
        if ((value < minValue) or (value > maxValue)) 
            return 0.0f;
        float d = std::min(value - minValue, maxValue - value);
        return (d >= threshold) ? 1.0f : d / threshold;
    }


    inline double Ramp(double value, double minValue, double maxValue, double threshold) noexcept {
        if (threshold < NumericTolerance)
            return 0.0f;
        if ((value < minValue) or (value > maxValue))
            return 0.0f;
        double d = std::min(value - minValue, maxValue - value);
        return (d >= threshold) ? 1.0f : d / threshold;
    }


    template <typename T, typename... Args>
    std::unique_ptr<T> MakeUnique(Args&&... args) {
        return std::unique_ptr<T>{ new(std::nothrow) T(std::forward<Args>(args)...) };
    }
};

// =================================================================================================
