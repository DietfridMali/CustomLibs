#pragma once

#include <cstdint>
#include "gfxtypes.h"   // GfxTypes::Int/Uint/Float/Enum/Handle — resolved per API via include path

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
    UInt32 = 1,
    UInt16 = 2
};

enum class GfxBufferTarget : uint8_t {
    Vertex = 0,
    Index  = 1
};

enum class GfxWrapMode : uint8_t {
    Repeat      = 0,
    ClampToEdge = 1
};

enum class TextureType : uint8_t {
    Texture2D = 0,
    Texture3D = 1,
    CubeMap   = 2
};

// =================================================================================================
