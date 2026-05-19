#pragma once

// =================================================================================================
// OpenGL ComputeShader
//
// Mirrors vulkan/include/compute_shader.h and directx/include/compute_shader.h but uses the
// native GL compute pipeline. GLSL compute source (#version 430 core minimum) is compiled via
// glCompileShader(GL_COMPUTE_SHADER) and linked into a standalone GL program.
//
// Binding model:
//   ComputeBindingDesc::Kind::UniformBuffer   -> layout(std140, binding=N) uniform CBV { ... }
//   ComputeBindingDesc::Kind::SampledImage    -> layout(binding=N) uniform sampler3D / sampler2D
//   ComputeBindingDesc::Kind::StorageImage    -> layout(binding=N, rgba16f) uniform image2D
//   ComputeBindingDesc::Kind::Sampler         -> not used (GL has combined sampler/texture)
//
// Inherits from Shader purely for the m_handle/m_uniforms/SetXxx uniform-cache machinery — the
// program is built via CreateCompute (compute stage only) instead of Create(vs, fs, gs).
//
// Resource binding helpers (BindSampledImage / BindStorageImage) wrap the GL state-setter calls
// (glActiveTexture + glBindTexture, glBindImageTexture). Caller is responsible for issuing
// glMemoryBarrier(...) between dispatches and subsequent reads when needed.
// =================================================================================================

#include "shader.h"
#include "base_shadercode.h"   // ComputeBindingDesc

class Texture;
class RenderTarget;

// =================================================================================================

class ComputeShader
    : public Shader
{
public:
    String                          m_cs;       // GLSL compute source (retained for reload/debug)
    AutoArray<ComputeBindingDesc>   m_bindings;

    ComputeShader(String name = "")
        : Shader(std::move(name))
    {
    }

    ~ComputeShader() = default;

    // Compile + link a compute-only program from GLSL source. bindings is retained for
    // descriptor-introspection (e.g. when the caller wants to validate slot usage); GL itself
    // resolves bindings from the explicit `layout(binding=N)` qualifiers inside the shader source.
    bool Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings);

    // glUseProgram on m_handle. Returns false if the program is invalid.
    bool Activate(void);

    // glDispatchCompute(x, y, z). Caller-supplied workgroup counts (already divided by local_size).
    bool Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

    // Convenience: dispatch over a 2D region with a chosen workgroup tile size. Rounds up.
    bool Dispatch2D(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY);

    // Bind a regular sampler-texture for read (layout(binding=unit) uniform sampler*).
    // GL uses combined sampler+texture; unit is the GL_TEXTURE0+unit slot.
    bool BindSampledImage(uint32_t unit, Texture* texture);

    // Bind a RenderTarget color-buffer as image2D for read/write
    // (layout(binding=unit, format) uniform image2D X). access is GL_READ_ONLY / GL_WRITE_ONLY /
    // GL_READ_WRITE. Internal format must match the shader's format qualifier (e.g. GL_RGBA16F).
    bool BindStorageImage(uint32_t unit, RenderTarget* target, int bufferIndex,
                          GLenum access = GL_READ_WRITE, GLenum internalFormat = GL_RGBA16F,
                          int level = 0);
};

// =================================================================================================
