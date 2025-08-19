#include "cube.h"

// =================================================================================================

Vector3f Cube::vertices[Cube::vertexCount] = {
    Vector3f{0, 0, 0},
    Vector3f{0, 0, 1},
    Vector3f{1, 0, 1},
    Vector3f{1, 0, 0},
    Vector3f{0, 1, 0},
    Vector3f{0, 1, 1},
    Vector3f{1, 1, 1},
    Vector3f{1, 1, 0}
};


GLuint Cube::triangleIndices[Cube::triangleIndexCount][3] = {
    {0, 1, 5}, {0, 5, 4}, // left
    {2, 3, 7}, {2, 7, 6}, // right
    {1, 2, 6}, {1, 6, 5}, // front
    {3, 0, 4}, {3, 4, 7}, // back
    {4, 5, 6}, {4, 6, 7}, // top
    {0, 2, 1}, {0, 3, 2}  // bottom
};


GLuint Cube::quadIndices[Cube::quadIndexCount][4] = {
    {0, 1, 5, 4},
    {2, 3, 7, 6},
    {1, 2, 6, 5},
    {3, 0, 4, 7},
    {4, 5, 6, 7},
    {3, 2, 1, 0}
};

// =================================================================================================
