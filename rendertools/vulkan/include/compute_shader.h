#pragma once

// =================================================================================================
// Vulkan Compute Shader
//
// Single-stage compute pipeline. HLSL CS source compiled via DXC (target cs_6_0) → SPIR-V →
// VkShaderModule + VkPipeline. DescriptorSetLayout configurable per-instance from a list of
// ComputeBindingDesc entries (UBO/SAMPLED_IMAGE/STORAGE_IMAGE/Sampler).
//
// Per-pass uniforms are written via name-based setters (SetFloat / SetMatrix4f / SetVector3f /
// SetInt / SetVector2i / …) — these resolve to offsets in the reflected b1 cbuffer (via
// spirv-reflect on Create). One source of truth (the HLSL cbuffer); no parallel C++ struct.
// =================================================================================================

#include <span>
#include <vector>
#include <cstdint>

#include "vkframework.h"
#include "string.hpp"
#include "array.hpp"
#include "vector.hpp"
#include "base_shadercode.h"   // ComputeBindingDesc

class Texture;
class RenderTarget;

// =================================================================================================

class ComputeShader
{
public:
    String                              m_name;
    String                              m_cs;       // CS HLSL source (reference / reload)
    std::vector<uint8_t>                m_csSpirv;  // SPIR-V bytecode
    VkShaderModule                      m_csModule{ VK_NULL_HANDLE };
    VkPipelineLayout                    m_pipelineLayout{ VK_NULL_HANDLE };
    VkDescriptorSetLayout               m_setLayout{ VK_NULL_HANDLE };
    VkPipeline                          m_pipeline{ VK_NULL_HANDLE };
    AutoArray<ComputeBindingDesc>       m_bindings;

    // Reflected b1 cbuffer layout — { fieldName -> {offset, size} } populated from spirv-reflect
    // on Create. Backed by m_b1Staging (sized to the reflected block size). SetB1Field looks up
    // the name and writes into the staging buffer. UploadB1 flushes to a UBO sub-allocation.
    struct FieldInfo { uint32_t offset{ 0 }; uint32_t size{ 0 }; };
    uint32_t                            m_b1Size{ 0 };
    AutoArray<std::pair<String, FieldInfo>> m_b1Fields;
    std::vector<uint8_t>                m_b1Staging;
    bool                                m_b1Dirty{ true };

    // After UploadB1 the per-frame UBO dynamic offset of the b1 binding lives here. Caller wires
    // it into pDynamicOffsets for vkCmdBindDescriptorSets.
    uint32_t                            m_b1DynamicOffset{ 0 };

    ComputeShader(String name = "")
        : m_name(std::move(name))
    {
    }

    ~ComputeShader() {
        Destroy();
    }

    bool Compile(const char* hlslCode, const char* entryPoint, std::vector<uint8_t>& spirvOut) noexcept;

    bool Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings);

    void Destroy(void) noexcept;

    inline bool IsValid(void) const noexcept {
        return (m_csModule != VK_NULL_HANDLE) and (m_pipeline != VK_NULL_HANDLE);
    }

    bool Activate(void);
    bool Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
    bool Dispatch2D(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY);

    bool BindSampledImage(uint32_t binding, Texture* texture, uint32_t arrayIndex = 0);
    bool BindStorageImage(uint32_t binding, RenderTarget* target, int bufferIndex, uint32_t arrayIndex = 0);
    bool BindSampler(uint32_t binding, VkSampler sampler);

    // -----------------------------------------------------------------------------------------
    // b1 uniform setters (name-based, reflection-resolved).

    // Low-level: write 'size' bytes at the reflected offset of 'name' in the b1 cbuffer.
    // Returns the offset on success or -1 if the name is not in the reflected layout.
    int SetB1Field(const char* name, const void* data, size_t size) noexcept;

    // Direct byte writes (escape hatch). offset/size are caller-supplied.
    int SetB1(uint32_t offset, const void* data, size_t size) noexcept;

    // Typed wrappers — mirror Graphics Shader::SetXxx so cloudrenderer.cpp can use one pattern.
    int SetFloat(const char* name, float data) noexcept;
    int SetInt(const char* name, int data) noexcept;
    int SetVector2f(const char* name, const Vector2f& data) noexcept;
    int SetVector3f(const char* name, const Vector3f& data) noexcept;
    int SetVector4f(const char* name, const Vector4f& data) noexcept;
    int SetVector2i(const char* name, const Vector2i& data) noexcept;
    int SetVector3i(const char* name, const Vector3i& data) noexcept;
    int SetVector4i(const char* name, const Vector4i& data) noexcept;
    int SetMatrix4f(const char* name, const float* data, bool transpose = false) noexcept;
    int SetMatrix3f(const char* name, const float* data, bool transpose = false) noexcept;

    // Sub-allocate m_b1Size bytes from cbvAllocator, memcpy m_b1Staging into it, stash the
    // dynamic offset in m_b1DynamicOffset for the next descriptor-set bind.
    bool UploadB1(void) noexcept;

private:
    bool CreatePipelineLayout(const AutoArray<ComputeBindingDesc>& bindings) noexcept;
    bool CreatePipeline(void) noexcept;

    // SPIR-V reflection: walk descriptor binding 1 (b1) and collect member { name, offset, size }.
    void ReflectB1Fields(void) noexcept;
};

// =================================================================================================
