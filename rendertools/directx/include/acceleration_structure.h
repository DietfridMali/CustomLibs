#pragma once

#include <cstdint>

// =================================================================================================
// AccelerationStructure (DirectX 12) - stub for now.
//
// DXR 1.1 inline ray tracing (RayQuery in HLSL, shader model 6.5) would carry this backend the
// same way VK_KHR_ray_query carries Vulkan, and the shader source is shared, so the effort is the
// C++ side: ID3D12Device5::CreateRaytracingAccelerationStructure plus a D3D12_RAYTRACING_TIER_1_1
// check on the device. Until that is built the class exists so the shared d2x-xl sources compile:
// every Build call fails and the caller keeps its non ray traced path.
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
