
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"
#include "compute_shader.h"

// =================================================================================================

const ShaderSource& TestShader();
const ShaderSource& StencilShader();
const ShaderSource& SurfaceShadowShader();
const ShaderSource& SphereShadowShader();
const ShaderSource& DepthRenderer();
const ShaderSource& LineShader();
const ShaderSource& RingShader();
const ShaderSource& CircleShader();
const ShaderSource& CircleMaskShader();
const ShaderSource& RectangleShader();
const ShaderSource& ShadedRectangleShader();
const ShaderSource& ShadedRingShader();
const ShaderSource& ColorMeshShader();
const ShaderSource& PlainColorShader();
const ShaderSource& PlainTextureShader();
const ShaderSource& GlyphShader();
const ShaderSource& MovingTextureShader();
const ShaderSource& BlurTextureShader();
const ShaderSource& GrayScaleShader();
const ShaderSource& TintAndBlurShader();
const ShaderSource& OutlineShader();
const ShaderSource& BevelShader();
const ShaderSource& BoxBlurShader();
const ShaderSource& ToneMapShader();
const ShaderSource& FxaaShader();
const ShaderSource& GaussBlurShader();
const ShaderSource& BilateralBlurShader();
const ShaderSource& BilateralBlurDepthShader();
const ShaderSource& LightningDrawShader();
const ShaderSource& LightningFlareShader();
const ShaderSource& LineDrawShader();
const ShaderSource& SkyboxShader();
const ShaderSource& BlackholeShader();

// -------------------------------------------------------------------------------------------------

BaseShaderCode::BaseShaderCode(const String& shaderFolder)
    : m_shaderFolder(shaderFolder)
{
    AutoArray<const ShaderSource*> shaderSource = {
        &TestShader(),
        &StencilShader(),
        &SurfaceShadowShader(),
        &SphereShadowShader(),
        &DepthRenderer(),
        &LineShader(),
        &RingShader(),
        &CircleShader(),
        &CircleMaskShader(),
        &RectangleShader(),
        &ShadedRectangleShader(),
        &ShadedRingShader(),
        &ColorMeshShader(),
        &PlainColorShader(),
        &PlainTextureShader(),
        &GlyphShader(),
        &MovingTextureShader(),
        &BlurTextureShader(),
        &GrayScaleShader(),
        &TintAndBlurShader(),
        &OutlineShader(),
        &BevelShader(),
        &BoxBlurShader(),
        &ToneMapShader(),
        &FxaaShader(),
        &GaussBlurShader(),
        &BilateralBlurShader(),
        &BilateralBlurDepthShader(),
        &LightningDrawShader(),
        &LightningFlareShader(),
        &LineDrawShader(),
        &SkyboxShader(),
        &BlackholeShader()
    };
    AddShaders(shaderSource);
}


static String FormatCompilerArgs(const AutoArray<ShaderMacro>& macros) {
    String out;
    for (const ShaderMacro& m : macros)
        out = out + String("#define ") + m.m_name + String(" ") + m.m_value + String("\n");
    return out;
}


void BaseShaderCode::AddShaders(AutoArray<const ShaderSource*>& shaderSource) {
    for (const ShaderSource* source : shaderSource)
        m_shaderSources.Append(source);
}


void BaseShaderCode::CreateShaders(void) {
    for (const ShaderSource* source : m_shaderSources) {
        if (source)
            CreateShader(source);
    }
    m_shaderSources.Clear();
}


void BaseShaderCode::CreateShaders(const AutoArray<String>& shaderIds) {
    for (const ShaderSource*& source : m_shaderSources) {
        if (not source)
            continue;
        for (const String& shaderId : shaderIds) {
            if (source->m_name == shaderId) {
                CreateShader(source);
                source = nullptr;
                break;
            }
        }
    }
}


void BaseShaderCode::CreateShader(const ShaderSource* source) {
    String prefix = FormatCompilerArgs(source->m_compilerArgs);
    if (source->IsCompute()) {
        String cs = prefix + source->m_cs;
        ComputeShader* shader = new ComputeShader(source->m_name);
        shader->m_cs = cs;
        if (shader->Create(cs, source->m_computeBindings, m_shaderFolder)) {
            m_computeShaders[source->m_name] = shader;
        }
        else {
#ifdef _DEBUG
            fprintf(stderr, "creating compute shader '%s' failed\n", (const char*)source->m_name);
#endif
            delete shader;
        }
        return;
    }
    String vs = prefix + source->m_vs;
    String fs = prefix + source->m_fs;
    String gs = source->m_gs.IsEmpty() ? String() : (prefix + source->m_gs);
    String tcs = source->m_tcs.IsEmpty() ? String() : (prefix + source->m_tcs);
    String tes = source->m_tes.IsEmpty() ? String() : (prefix + source->m_tes);
    Shader* shader = new Shader(source->m_name, vs, fs, gs);
    shader->m_dataLayout = source->m_dataLayout;
    if (shader->Create(vs, fs, gs, tcs, tes, m_shaderFolder))
        m_shaders[source->m_name] = shader;
    else {
#ifdef _DEBUG
        fprintf(stderr, "creating shader '%s' failed\n", (const char*) source->m_name);
#endif
        delete shader;
    }
}

// =================================================================================================
