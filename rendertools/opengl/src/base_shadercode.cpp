
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"
#include "compute_shader.h"

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
const ShaderSource& PlainColorShader();
const ShaderSource& ColorMeshShader();
const ShaderSource& PlainTextureShader();
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
        &PlainColorShader(),
        &ColorMeshShader(),
        &PlainTextureShader(),
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


void BaseShaderCode::AddShaders(AutoArray<const ShaderSource*>& shaderSource) {
    for (const ShaderSource* source : shaderSource) {
        if (not gfxStates.HaveFeatureLevel(source->m_featureLevel)) {
            if (source->IsCompute())
                m_computeShaders[source->m_name] = nullptr;
            else
                m_shaders[source->m_name] = nullptr;
            continue;
        }
        if (source->IsCompute()) {
            ComputeShader* shader = new ComputeShader(source->m_name);
            if (shader->Create(source->m_cs, source->m_computeBindings))
                m_computeShaders[source->m_name] = shader;
            else {
                m_computeShaders[source->m_name] = nullptr;
                delete shader;
#ifdef _DEBUG
                fprintf(stderr, "creating compute shader '%s' failed\n", (const char*)source->m_name);
#endif
            }
        }
        else {
            Shader* shader = new Shader(source->m_name);
            if (shader->Create(source->m_vs, source->m_fs, source->m_gs))
                m_shaders[source->m_name] = shader;
            else {
                m_shaders[source->m_name] = nullptr;
                delete shader;
#ifdef _DEBUG
                fprintf(stderr, "creating shader '%s' failed\n", (const char*)source->m_name);
#endif
            }
        }
    }
}

// =================================================================================================
