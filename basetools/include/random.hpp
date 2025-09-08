#pragma once

#include <random>
#include "singletonbase.hpp"

// =================================================================================================

class Random 
    : public BaseSingleton<Random> 
{
private:
    inline static thread_local std::mt19937 rng{ std::random_device{}() };
    inline static thread_local std::uniform_real_distribution<float> floatDist{ 0.0f, 1.0f };

public:
    static float Float(float scale = 1.0f)
    {
        return floatDist(rng) * scale; // [0,1)
    }

    static int Int(int maxValue) // [0, maxValue-1]
    {
        return static_cast<int>(Float() * maxValue);
    }

    static int Int(int minValue, int maxValue) // [minValue, maxValue]
    {
        return Int(maxValue - minValue + 1) + minValue;
    }
};

#define random Random::Instance()

// =================================================================================================
