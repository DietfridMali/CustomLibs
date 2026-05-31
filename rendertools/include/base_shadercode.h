#pragma once

#include <string>
#include <utility>
#include <map>
#include <type_traits>

#include "shader.h"
#include "string.hpp"
#include "dictionary.hpp"

// =================================================================================================

#include "shaderdatalayout.h"

// =================================================================================================
// GLSLVersion — emits a "#version <value>\n" line to prepend to GLSL shader source. Lives here so
// the OpenGL shader sources (which include this via shadercode.h) and the HLSL→GLSL bridge
// (hlslbridge.inl) share one definition.

inline String GLSLVersion(const char* value = "430 core") {
    return (value and *value) ? String("#version ") + String(value) + String("\n") : String("");
}

// =================================================================================================

struct ShaderMacro {
    String  m_name{ "" };
    String  m_value{ "" };
};

// =================================================================================================
// ComputeBindingDesc — describes one slot of a compute shader's descriptor set layout.
// Used by ShaderSource for compute sources and by ComputeShader::Create when building the
// VkDescriptorSetLayout / D3D12 root signature.

struct ComputeBindingDesc {
    enum class Kind {
        UniformBuffer,
        SampledImage,
        StorageImage,
        StorageBuffer,
        Sampler,
        CombinedImageSampler
    };
    uint32_t    binding{ 0 };
    Kind        kind{ Kind::UniformBuffer };
    uint32_t    count{ 1 };
};

// =================================================================================================

struct ShaderSourceParams {
    String                          vs{ "" };
    String                          fs{ "" };
    String                          gs{ "" };
    // Compute source — when non-empty, the shader is treated as a compute shader (vs/fs/gs
    // should be empty in that case). Target profile is cs_6_0 (DXC). Compute shaders skip the
    // graphics PSO / vertex-input setup; instead the backend builds a compute pipeline using
    // computeBindings as the descriptor-set layout source.
    String                          cs{ "" };
    AutoArray<ComputeBindingDesc>   computeBindings{};
    AutoArray<ShaderMacro>          compilerArgs;
    ShaderDataLayout                dataLayout;
    int                             featureLevel{ 0 };
};

// =================================================================================================

class ShaderSource {
public:
    using KeyType = String;

    String                          m_name{ "" };
    String                          m_vs{ "" };
    String                          m_fs{ "" };
    String                          m_gs{ "" };
    String                          m_cs{ "" };
    AutoArray<ComputeBindingDesc>   m_computeBindings{};
    mutable AutoArray<ShaderMacro>  m_compilerArgs{};
    int                             m_featureLevel{ 0 };
    ShaderDataLayout                m_dataLayout;

    ShaderSource() = default;

    explicit ShaderSource(String name, const ShaderSourceParams& params)
        : m_name(name)
        , m_vs(params.vs)
        , m_fs(params.fs)
        , m_gs(params.gs)
        , m_cs(params.cs)
        , m_computeBindings(params.computeBindings)
        , m_compilerArgs(params.compilerArgs)
        , m_featureLevel(params.featureLevel)
        , m_dataLayout(params.dataLayout)
    { }

    explicit ShaderSource(String name, String vs, String fs, String gs = "", AutoArray<ShaderMacro> compilerArgs = {}, int featureLevel = 0)
        : m_name(name)
        , m_vs(vs)
        , m_fs(fs)
        , m_gs(gs)
        , m_compilerArgs(compilerArgs)
        , m_featureLevel(featureLevel)
    { }

    explicit ShaderSource(String name, String vs, String fs, ShaderDataLayout layout, AutoArray<ShaderMacro> compilerArgs = {}, int featureLevel = 0)
        : m_name(name)
        , m_vs(vs)
        , m_fs(fs)
        , m_compilerArgs(compilerArgs)
        , m_featureLevel(featureLevel)
        , m_dataLayout(layout)
    { }

    explicit ShaderSource(String name, String vs, String fs, String gs, ShaderDataLayout layout, AutoArray<ShaderMacro> compilerArgs = {}, int featureLevel = 0)
        : m_name(name)
        , m_vs(vs)
        , m_fs(fs)
        , m_gs(gs)
        , m_compilerArgs(compilerArgs)
        , m_featureLevel(featureLevel)
        , m_dataLayout(layout)
    { }

    ShaderSource(const ShaderSource& other)
        : m_name(other.m_name)
        , m_vs(other.m_vs)
        , m_fs(other.m_fs)
        , m_gs(other.m_gs)
        , m_cs(other.m_cs)
        , m_computeBindings(other.m_computeBindings)
        , m_compilerArgs(other.m_compilerArgs)
        , m_featureLevel(other.m_featureLevel)
        , m_dataLayout(other.m_dataLayout)
    {
    }

    inline String& GetKey(void) {
        return m_name;
    }

    inline bool IsCompute(void) const {
        return not m_cs.IsEmpty();
    }

    inline void SetCompilerArgs(const AutoArray<ShaderMacro>& args) const {
        m_compilerArgs = args;
    }
#if 0    
    inline const bool operator< (ShaderSource const& other) {
        return m_name < other.m_name;
    }

    inline const bool operator> (ShaderSource const& other) {
        return m_name > other.m_name;
    }

    inline const bool operator<= (ShaderSource const& other) {
        return m_name <= other.m_name;
    }

    inline const bool operator>= (ShaderSource const& other) {
        return m_name >= other.m_name;
    }

    inline const bool operator!= (ShaderSource const& other) {
        return m_name != other.m_name;
    }

    inline const bool operator== (ShaderSource const& other) {
        return m_name == other.m_name;
    }
    
    static int Compare(const ShaderSource* o1, const ShaderSource* o2) {
        return String::Compare(nullptr, o1->m_name, o2->m_name);
    }
#endif
};

// =================================================================================================

const String& Standard2DVS();
const String& Standard3DVS();
const String& Offset2DVS();
const String& GaussBlurFuncs();
const String& BoostFuncs();
const String& SRGBFuncs();
const String& TintFuncs();
const String& NoiseFuncs();
const String& RandFuncs();
const String& ChromAbFuncs();
const String& EdgeFadeFunc();
const String& VignetteFunc();

// =================================================================================================

class ComputeShader;

class BaseShaderCode
    : public Shader
{
protected:
    Dictionary<String, Shader*>         m_shaders;
    Dictionary<String, ComputeShader*>  m_computeShaders;

public:
    BaseShaderCode();
    ~BaseShaderCode() = default;

    void AddShaders(AutoArray<const ShaderSource*>& shaderSource);

    inline Shader* GetShader(String shaderId) {
        Shader** shader = m_shaders.Find(shaderId);
        return shader ? *shader : nullptr;
    }

    inline ComputeShader* GetComputeShader(String shaderId) {
        ComputeShader** shader = m_computeShaders.Find(shaderId);
        return shader ? *shader : nullptr;
    }
};

// =================================================================================================

