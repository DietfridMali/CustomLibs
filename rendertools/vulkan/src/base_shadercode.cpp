
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& TestShader();
const ShaderSource& StencilShader();
const ShaderSource& ShadowShader();
const ShaderSource& SphereShadowShader();
const ShaderSource& DepthRenderer();
const ShaderSource& LineShader();
const ShaderSource& RingShader();
const ShaderSource& CircleShader();
const ShaderSource& CircleMaskShader();
const ShaderSource& RectangleShader();
const ShaderSource& ColorMeshShader();
const ShaderSource& PlainColorShader();
const ShaderSource& PlainTextureShader();
const ShaderSource& GlyphShader();
const ShaderSource& MovingTextureShader();
const ShaderSource& BlurTextureShader();
const ShaderSource& GrayScaleShader();
const ShaderSource& TintAndBlurShader();
const ShaderSource& OutlineShader();
const ShaderSource& BoxBlurShader();
const ShaderSource& FxaaShader();
const ShaderSource& GaussBlurShader();

// -------------------------------------------------------------------------------------------------

BaseShaderCode::BaseShaderCode() {
    AutoArray<const ShaderSource*> shaderSource = {
        &TestShader(),
        &StencilShader(),
        &ShadowShader(),
        &SphereShadowShader(),
        &DepthRenderer(),
        &LineShader(),
        &RingShader(),
        &CircleShader(),
        &CircleMaskShader(),
        &RectangleShader(),
        &ColorMeshShader(),
        &PlainColorShader(),
        &PlainTextureShader(),
        &GlyphShader(),
        &MovingTextureShader(),
        &BlurTextureShader(),
        &GrayScaleShader(),
        &TintAndBlurShader(),
        &OutlineShader(),
        &BoxBlurShader(),
        &FxaaShader(),
        &GaussBlurShader()
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
    for (const ShaderSource* source : shaderSource) {
        String prefix = FormatCompilerArgs(source->m_compilerArgs);
        String vs = prefix + source->m_vs;
        String fs = prefix + source->m_fs;
        String gs = source->m_gs.IsEmpty() ? String() : (prefix + source->m_gs);
        Shader* shader = new Shader(source->m_name, vs, fs, gs);
        shader->m_dataLayout = source->m_dataLayout;
        if (shader->Create(vs, fs, gs))
            m_shaders[source->m_name] = shader;
#ifdef _DEBUG
        else
            fprintf(stderr, "creating shader '%s' failed\n", (const char*) source->m_name);
#endif
    }
}

// =================================================================================================
