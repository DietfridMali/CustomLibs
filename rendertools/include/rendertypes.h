#pragma once

#include <cstdint>

// =================================================================================================
// API-neutral enumerations for mesh topology, component data types, and texture types.
// OGL implementations convert these to GLenum internally; DX12 implementations use DXGI_FORMAT etc.

enum class MeshTopology : uint8_t {
    Quads     = 0,
    Triangles = 1,
    Lines     = 2,
    Points    = 3
};

enum class ComponentType : uint8_t {
    Float  = 0,
    UInt32 = 1
};

enum class TextureType : uint8_t {
    Texture2D = 0,
    Texture3D = 1,
    CubeMap   = 2
};

// =================================================================================================
