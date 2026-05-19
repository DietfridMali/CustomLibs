#pragma once

// =================================================================================================
// DX12 ComputeShader
//
// Mirrors vulkan/include/compute_shader.h. HLSL CS source is compiled via DXC (cs_6_0) to DXIL.
// Root signature is built from the caller-supplied AutoArray<ComputeBindingDesc>:
//
//   ComputeBindingDesc::Kind::UniformBuffer   -> Root CBV   (b<binding>)
//   ComputeBindingDesc::Kind::SampledImage    -> SRV table  (t<binding-mapped>)
//   ComputeBindingDesc::Kind::Sampler         -> Sampler table (s<binding-mapped>)
//   ComputeBindingDesc::Kind::StorageImage    -> UAV table  (u<binding-mapped>)
//
// Binding numbers from the descriptor mirror the SPIR-V table used by the Vulkan compute
// shader (b0/b1, t0..t9, s0..s9, u0). DXC compiles the same HLSL source against the same
// register names natively for DX12 — no -fvk-bind-register mapping needed.
//
// Activate/Dispatch are minimal — the caller (CloudRenderer's TSP driver) drives the
// SetComputeRootDescriptorTable / SetComputeRootCBV / Dispatch calls directly on the command
// list, identical to how the Vulkan path works.
// =================================================================================================

#include <vector>
#include <cstdint>

#include "dx12framework.h"
#include "string.hpp"
#include "array.hpp"
#include "vector.hpp"
#include "base_shadercode.h"   // ComputeBindingDesc

#include <d3d12.h>
#include <wrl/client.h>

class Texture;
class RenderTarget;

// =================================================================================================

class ComputeShader
{
public:
    String                              m_name;
    String                              m_cs;
    Microsoft::WRL::ComPtr<ID3DBlob>    m_csBytecode;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
    AutoArray<ComputeBindingDesc>       m_bindings;

    // Reflected b1 cbuffer layout — { fieldName -> {offset, size} } populated from DXIL reflection
    // on Create. Backed by m_b1Staging (sized to the reflected block size). SetB1Field looks up
    // the name and writes into the staging buffer. UploadB1 flushes to a CBV sub-allocation.
    struct FieldInfo { uint32_t offset{ 0 }; uint32_t size{ 0 }; };
    uint32_t                            m_b1Size{ 0 };
    AutoArray<std::pair<String, FieldInfo>> m_b1Fields;
    std::vector<uint8_t>                m_b1Staging;
    bool                                m_b1Dirty{ true };

    // After UploadB1 the per-frame CBV GPU virtual address lives here. Caller passes it to
    // SetComputeRootConstantBufferView(m_cbvRootIndex[1], m_b1GpuVA).
    D3D12_GPU_VIRTUAL_ADDRESS           m_b1GpuVA{ 0 };

    // Slot maps populated by CreateRootSignature, indexed by HLSL register number.
    // Root parameter index for each binding kind, or -1 if not in the layout.
    int32_t                             m_cbvRootIndex[2]{ -1, -1 };   // b0, b1
    int32_t                             m_srvRootIndex[16];            // t0..t15
    int32_t                             m_samplerRootIndex[16];        // s0..s15
    int32_t                             m_uavRootIndex[4];             // u0..u3

    ComputeShader(String name = "")
        : m_name(std::move(name))
    {
        for (int i = 0; i < 16; ++i) { m_srvRootIndex[i] = -1; m_samplerRootIndex[i] = -1; }
        for (int i = 0; i < 4; ++i) m_uavRootIndex[i] = -1;
    }

    ~ComputeShader() {
        Destroy();
    }

    bool Compile(const char* hlslCode, const char* entryPoint) noexcept;

    bool Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings);

    void Destroy(void) noexcept;

    inline bool IsValid(void) const noexcept {
        return m_csBytecode and m_pipeline and m_rootSignature;
    }

    // Caller is responsible for SetComputeRootSignature + SetPipelineState + table binds +
    // Dispatch on the active command list. These two helpers are mostly for symmetry with the
    // Vulkan path.
    bool Activate(void);
    bool Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
    bool Dispatch2D(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY);

    // b1 — per-pass constants. Name-based setters resolve to reflected offsets in the cbuffer.
    int SetB1Field(const char* name, const void* data, size_t size) noexcept;
    int SetB1(uint32_t offset, const void* data, size_t size) noexcept;

    int SetFloat   (const char* name, float data)            noexcept;
    int SetInt     (const char* name, int data)              noexcept;
    int SetVector2f(const char* name, const Vector2f& data)  noexcept;
    int SetVector3f(const char* name, const Vector3f& data)  noexcept;
    int SetVector4f(const char* name, const Vector4f& data)  noexcept;
    int SetVector2i(const char* name, const Vector2i& data)  noexcept;
    int SetVector3i(const char* name, const Vector3i& data)  noexcept;
    int SetVector4i(const char* name, const Vector4i& data)  noexcept;
    int SetMatrix4f(const char* name, const float* data, bool transpose = false) noexcept;
    int SetMatrix3f(const char* name, const float* data, bool transpose = false) noexcept;

    // Sub-allocate m_b1Size bytes from cbvAllocator, memcpy m_b1Staging into it, stash the GPU
    // virtual address in m_b1GpuVA for the next SetComputeRootConstantBufferView call.
    bool UploadB1(void) noexcept;

private:
    bool CreateRootSignature(const AutoArray<ComputeBindingDesc>& bindings) noexcept;
    bool CreatePipeline(void) noexcept;
    void ReflectB1Fields(void) noexcept;
};

// =================================================================================================
