#pragma once

#include <random>
#include <cstdint>
#include "basesingleton.hpp"

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
        return floatDist(rng) * scale; // [0,scale)
    }

    static float Float(float minValue, float maxValue) // [minValue, maxValue)
    {
        return minValue + floatDist(rng) * (maxValue - minValue);
    }

    static int Int(int maxValue) // [0, maxValue-1]
    {
        return static_cast<int>(Float() * maxValue);
    }

    static int Int(int minValue, int maxValue) // [minValue, maxValue]
    {
        return Int(maxValue - minValue + 1) + minValue;
    }

    // reproducible sequences: consumers that have to stay in sync (network peers, recorded
    // demos) seed every participant with the same value
    static void Seed(uint32_t seed)
    {
        rng.seed(seed);
        floatDist.reset();
    }

    static void Seed(void)
    {
        rng.seed(std::random_device{}());
        floatDist.reset();
    }
};

#define random Random::Instance()

// =================================================================================================
