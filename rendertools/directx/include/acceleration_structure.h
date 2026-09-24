#pragma once

#include "dx12framework.h"

#include <cstdint>

// =================================================================================================

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
    bool Load (ID3D12Device* device) noexcept;

    bool IsLoaded (void) noexcept;

    uint32_t ScratchAlignment (void) noexcept;
}


struct AccelBuildItem
{
    class AccelerationStructure* structure     { nullptr };
    const AccelGeometryDesc*     geometries    { nullptr };
    uint32_t                     geometryCount { 0 };
};


class AccelerationStructure
{
public:
    static constexpr uint32_t   kFrameSlots = 2;

    ComPtr<ID3D12Resource>      m_storage [kFrameSlots];
    UINT64                      m_storageSize [kFrameSlots] { 0, 0 };
    ComPtr<ID3D12Resource>      m_instances [kFrameSlots];
    uint32_t                    m_instanceCapacity [kFrameSlots] { 0, 0 };
    ComPtr<ID3D12Resource>      m_scratch [kFrameSlots];
    UINT64                      m_scratchSize [kFrameSlots] { 0, 0 };
    uint32_t                    m_slot { 0 };

    ComPtr<ID3D12Resource>      m_vertices;
    ComPtr<ID3D12Resource>      m_indices;
    D3D12_GPU_VIRTUAL_ADDRESS   m_address { 0 };
    uint32_t                    m_primitives { 0 };

    AccelerationStructure (void) noexcept = default;

    AccelerationStructure (const AccelerationStructure&) = delete;
    AccelerationStructure& operator= (const AccelerationStructure&) = delete;

    AccelerationStructure (AccelerationStructure&& other) noexcept { Move (other); }

    AccelerationStructure& operator= (AccelerationStructure&& other) noexcept {
        if (this != &other) {
            Destroy ();
            Move (other);
        }
        return *this;
    }

    static bool BuildBottomLevelBatch (const AccelBuildItem* items, uint32_t itemCount) noexcept;

    bool BuildBottomLevel (const AccelGeometryDesc* geometries, uint32_t geometryCount) noexcept;

    bool BuildTopLevel (const AccelInstance* instances, uint32_t instanceCount, bool immediate = false) noexcept;

    bool Bind (void) const noexcept;

    void Destroy (void) noexcept;

    inline D3D12_GPU_VIRTUAL_ADDRESS Handle (void) const noexcept { return m_storage [m_slot] ? m_storage [m_slot]->GetGPUVirtualAddress () : 0; }
    inline D3D12_GPU_VIRTUAL_ADDRESS DeviceAddress (void) const noexcept { return m_address; }
    inline uint32_t Primitives (void) const noexcept { return m_primitives; }
    inline bool IsValid (void) const noexcept { return m_storage [m_slot] != nullptr; }

private:
    void Move (AccelerationStructure& other) noexcept;
};

// =================================================================================================
