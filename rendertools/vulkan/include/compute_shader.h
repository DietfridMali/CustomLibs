#pragma once

// =================================================================================================
// Vulkan Compute Shader — Architecture sketch (2026-05-18)
//
// STATUS: Header-only skeleton, NOT included in any build target. Lives as a concrete API
//         proposal for the Compute pipeline layer requested as part of the FastVolumetricClouds /
//         Temporal Sky Probe work. Implementation (compute_shader.cpp) and wiring into the
//         build / pipeline cache come in a follow-up step.
//
// Mirrors the Shader class structure (vulkan/include/shader.h) with the following differences:
//   - Single SPIR-V stage (compute), target "cs_6_0".
//   - DescriptorSetLayout is configurable per shader instance (UBO + sampled images + storage
//     images / storage buffers). Sky-Map update needs at least one STORAGE_IMAGE binding for
//     the destination map plus the existing texture set (shape noise, MaxMip pyramid, blue
//     noise, region noise, history texture).
//   - Pipeline created via vkCreateComputePipelines.
//   - Bind point VK_PIPELINE_BIND_POINT_COMPUTE in vkCmdBindDescriptorSets and vkCmdBindPipeline.
//   - Dispatch via vkCmdDispatch(x, y, z) with a workgroup-aware caller (Sky-Map update uses
//     8x8 threads → dispatch ceil(W/8) x ceil(H/8) groups).
//   - No vertex input, no render pass scope. Issued outside any render pass.
//
// LAYOUT TRANSITIONS
//   Storage-Image writes require VK_IMAGE_LAYOUT_GENERAL on the destination. Caller (or a
//   wrapper around ComputeShader::Dispatch) is responsible for transitioning the image from
//   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL → VK_IMAGE_LAYOUT_GENERAL before dispatch and back
//   to SHADER_READ_ONLY_OPTIMAL afterwards. The existing image_layout_tracker.h can be reused.
//
// HOW THE SKY-MAP UPDATE PASS USES THIS
//   Shader bindings (proposed):
//     binding 0  UBO              CS         b0 (FrameConstants — only the bits compute needs)
//     binding 1  UBO              CS         b1 (per-pass constants: frameIndex, N_accum,
//                                              haltonOffsetCount, mapSize, ...)
//     binding 2  SAMPLED_IMAGE    CS         shapeNoise  (t2)
//     binding 3  SAMPLED_IMAGE    CS         blueNoise   (t1)
//     binding 4..7 SAMPLED_IMAGE  CS         shapeNoiseMip1..4 (t3..t6)
//     binding 8  SAMPLED_IMAGE    CS         warpNoise   (t7)
//     binding 9  SAMPLED_IMAGE    CS         regionNoise (t8)
//     binding 10 SAMPLED_IMAGE    CS         skyMapHistory (read-side of the ping-pong pair)
//     binding 11 STORAGE_IMAGE    CS         skyMapTarget  (write-side of the ping-pong pair)
//     binding 12 SAMPLER          CS         shared sampler (clamp/linear)
//
// =================================================================================================

#include <vector>
#include <cstdint>

#include "vkframework.h"
#include "string.hpp"
#include "array.hpp"
#include "base_shadercode.h"   // ComputeBindingDesc lives here for ShaderSource use

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

    // Per-shader b1 staging buffer for push constants and per-pass uniforms.
    std::vector<uint8_t>                m_b1Staging;
    bool                                m_b1Dirty{ true };

    ComputeShader(String name = "")
        : m_name(std::move(name))
    {
    }

    ~ComputeShader() {
        Destroy();
    }

    // -----------------------------------------------------------------------------------------
    // Creation

    bool Compile(const char* hlslCode, const char* entryPoint, std::vector<uint8_t>& spirvOut) noexcept;

    // bindings: descriptor set layout description (UBO/SAMPLED_IMAGE/STORAGE_IMAGE/...).
    // Creates m_setLayout + m_pipelineLayout + m_pipeline. Returns false on any step failure.
    bool Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings);

    void Destroy(void) noexcept;

    inline bool IsValid(void) const noexcept {
        return (m_csModule != VK_NULL_HANDLE) and (m_pipeline != VK_NULL_HANDLE);
    }

    // -----------------------------------------------------------------------------------------
    // Runtime

    // vkCmdBindPipeline with VK_PIPELINE_BIND_POINT_COMPUTE on the active CommandList.
    bool Activate(void);

    // groupCountX/Y/Z are dispatched workgroup counts (already divided by workgroup size).
    // Caller is responsible for source-image layout transitions to GENERAL prior and back to
    // SHADER_READ_ONLY_OPTIMAL afterwards (see image_layout_tracker.h).
    bool Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

    // Convenience: dispatch over a 2D region with a chosen workgroup tile size.
    bool Dispatch2D(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY);

    // -----------------------------------------------------------------------------------------
    // Resource binding

    // Bind a Texture for read (SAMPLED_IMAGE binding).
    bool BindSampledImage(uint32_t binding, Texture* texture, uint32_t arrayIndex = 0);

    // Bind a RenderTarget color buffer as STORAGE_IMAGE for write. The target image must have
    // VK_IMAGE_USAGE_STORAGE_BIT set at creation; see rt_storage_image_extension below.
    bool BindStorageImage(uint32_t binding, RenderTarget* target, int bufferIndex, uint32_t arrayIndex = 0);

    bool BindSampler(uint32_t binding, VkSampler sampler);

    // -----------------------------------------------------------------------------------------
    // b1 — per-pass constants

    // Write 'size' bytes at 'offset' into m_b1Staging. Flushes to a UBO sub-allocation on the
    // next Activate() (analogous to Shader::UploadB1).
    int SetB1(uint32_t offset, const void* data, size_t size) noexcept;

    bool UploadB1(void) noexcept;

private:
    bool CreatePipelineLayout(const AutoArray<ComputeBindingDesc>& bindings) noexcept;

    bool CreatePipeline(void) noexcept;
};

// =================================================================================================
// Notes / open follow-up items (do not implement before user signs off):
//
// 1. RenderTarget needs an explicit storage-image flag at Create time so that the underlying
//    VkImage carries VK_IMAGE_USAGE_STORAGE_BIT. Today's RT defaults to color-attachment + sampled.
//    Proposal: extend RenderTarget::BufferInfo with a `bool storageImage` flag; CreateBuffer
//    ORs in VK_IMAGE_USAGE_STORAGE_BIT when set. Backwards compatible (default false).
//
// 2. The ComputeShader class currently presumes one descriptor set per shader (set 0). The
//    Graphics-Shader uses the same convention. If TSP needs cross-set sharing (e.g. shared
//    sampler with the cumulus draw), revisit.
//
// 3. Dispatch is currently graphics-queue. For better scheduling on dGPUs an async-compute
//    queue would be ideal but adds queue-family-ownership transitions on the storage image.
//    Out of scope for the initial implementation.
//
// 4. Reflection: the b1 layout for compute is reflected from SPIR-V on link, identical to the
//    graphics Shader path. Same reflection code can be reused (refactor shader.cpp::UpdateStageFields
//    to take a stage-enum and SPIR-V blob; no behavior change for graphics).
//
// 5. PipelineCache: compute pipelines don't depend on RenderStates or RT formats, so the cache
//    key is just (ComputeShader*, hash(bindings)). Smaller than the graphics PSO cache; can sit
//    in its own map keyed on m_name.
//
// =================================================================================================
