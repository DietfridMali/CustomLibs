#pragma once

#include "vkframework.h"
#include "string.hpp"

#include <vector>
#include <cstdint>

// =================================================================================================
// shader_compiler — DXC wrapper for HLSL → SPIR-V compilation, plus VkShaderModule helper.
//
// DXC (DirectX Shader Compiler) ships with the Vulkan SDK and can emit SPIR-V from HLSL
// source code via the `-spirv` flag. This wrapper hides the COM-based IDxcCompiler3 / IDxcUtils
// API behind plain C++ calls used by class Shader.
//
// Set / binding layout for the b/t/s/u register classes is the caller's responsibility — pass the
// matching `-fvk-b-shift`, `-fvk-t-shift`, `-fvk-s-shift`, `-fvk-u-shift` arguments via extraArgs.
// Phase B step 7c (PipelineLayout / DescriptorSetLayout build) will set these consistently with
// the descriptor-set design.

namespace ShaderCompiler
{
    // Loads dxcompiler.dll (delay-loaded by linker), creates IDxcUtils + IDxcCompiler3 singletons.
    // Idempotent — repeat calls are no-ops. Returns false on any error.
    bool Initialize(void) noexcept;

    // Releases the DXC singletons. Call once at app shutdown.
    void Shutdown(void) noexcept;

    // Compiles a HLSL source string to SPIR-V bytecode.
    //
    //   hlslSource    — null-terminated UTF-8 HLSL source
    //   entryPoint    — e.g. "VSMain" / "PSMain" / "GSMain"
    //   targetProfile — e.g. "vs_6_0" / "ps_6_0" / "gs_6_0"
    //   extraArgs     — additional command-line args (UTF-16, in DXC's wchar_t*-array form);
    //                   typical entries are -fvk-b-shift, -fvk-t-shift, -fvk-s-shift,
    //                   -fvk-u-shift, -D NAME=VALUE etc.
    //   outSpirv      — receives the SPIR-V blob (4-byte aligned, as words but stored as bytes here)
    //   outError      — receives the DXC compile log on failure (warnings are not surfaced)
    //
    // Returns true on success.
    bool CompileHlslToSpirv(const char* hlslSource,
                            const char* entryPoint,
                            const char* targetProfile,
                            const wchar_t* const* extraArgs,
                            uint32_t extraArgsCount,
                            std::vector<uint8_t>& outSpirv,
                            String& outError) noexcept;

    // Creates a VkShaderModule from raw SPIR-V bytes (must be 4-byte-aligned multiple).
    // Returns VK_NULL_HANDLE on failure.
    VkShaderModule CreateShaderModule(const std::vector<uint8_t>& spirv) noexcept;

    // Convenience: destroy a previously created VkShaderModule via vkContext.Device().
    void DestroyShaderModule(VkShaderModule module) noexcept;
}

// =================================================================================================
