
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
const ShaderSource& PlainColorShader();
const ShaderSource& ColorMeshShader();
const ShaderSource& PlainTextureShader();
const ShaderSource& MovingTextureShader();
const ShaderSource& BlurTextureShader();
const ShaderSource& GrayScaleShader();
const ShaderSource& TintAndBlurShader();
const ShaderSource& OutlineShader();
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
        &PlainColorShader(),
        &ColorMeshShader(),
        &PlainTextureShader(),
        &MovingTextureShader(),
        &BlurTextureShader(),
        &GrayScaleShader(),
        &TintAndBlurShader(),
        &OutlineShader(),
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
    if (not gfxStates.HaveFeatureLevel(source->m_featureLevel)) {
        if (source->IsCompute())
            m_computeShaders[source->m_name] = nullptr;
        else
            m_shaders[source->m_name] = nullptr;
        return;
    }
    if (source->IsCompute()) {
        ComputeShader* shader = new ComputeShader(source->m_name);
        if (shader->Create(source->m_cs, source->m_computeBindings, m_shaderFolder))
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
        if (shader->Create(source->m_vs, source->m_fs, source->m_gs, source->m_tcs, source->m_tes, m_shaderFolder))
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

// =================================================================================================
