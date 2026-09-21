#pragma once

#include <cstdint>

// =================================================================================================
// AccelerationStructure (OpenGL) - minimal stub, the way MeshHandler is one here.
//
// Hardware ray tracing needs VK_KHR_acceleration_structure or DXR; OpenGL 4.3 core has neither,
// and there is no extension to fall back on. The class exists so that the shared d2x-xl sources
// compile for this backend: every Build call fails, IsLoaded () is false, and the caller keeps
// whatever it does without ray tracing (the segment walk in raygeometry.h).
//
// The real implementation lives in rendertools/vulkan. Signatures must stay identical.

struct AccelGeometryDesc
{
    const float*    vertices     { nullptr };
    uint32_t        vertexCount  { 0 };
    const uint32_t* indices      { nullptr };
    uint32_t        indexCount   { 0 };
    bool            opaque       { true };
};


struct AccelInstance
{
    float           transform [12] { 1.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f };
    uint32_t        customIndex  { 0 };
    uint32_t        mask         { 0xFFu };
    bool            singleSided  { false };
    const class AccelerationStructure* blas { nullptr };
};


namespace RayTracingApi
{
    inline bool IsLoaded(void) noexcept { return false; }

    inline uint32_t ScratchAlignment(void) noexcept { return 0; }
}


class AccelerationStructure
{
public:
    bool BuildBottomLevel(const AccelGeometryDesc*, uint32_t) noexcept { return false; }

    bool BuildTopLevel(const AccelInstance*, uint32_t, bool = false) noexcept { return false; }

    bool Bind(void) const noexcept { return false; }

    void Destroy(void) noexcept { }

    inline uint32_t Primitives(void) const noexcept { return 0; }

    inline bool IsValid(void) const noexcept { return false; }
};

// =================================================================================================
