#pragma once

#include <cstdint>
#include "vector.hpp"
#include "basesingleton.hpp"

class Cube
    : public BaseSingleton<Cube>
{
public:
    Cube() = default;
    ~Cube() = default;

    static constexpr int vertexCount = 8;

    static Vector3f vertices[vertexCount];

    static constexpr int triangleIndexCount = 12;

    static uint32_t triangleIndices[triangleIndexCount][3];

    static constexpr int quadIndexCount = 6;

    static uint32_t quadIndices[quadIndexCount][4];
};