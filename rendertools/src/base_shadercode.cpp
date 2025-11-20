
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& DepthShader();
const ShaderSource& ActorDepthShader();
const ShaderSource& DepthRenderer();
const ShaderSource& LineShader();
const ShaderSource& RingShader();
const ShaderSource& CircleShader();
const ShaderSource& CircleMaskShader();
const ShaderSource& RectangleShader();
const ShaderSource& PlainColorShader();
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
    ManagedArray<const ShaderSource*> shaderSource = {
        &DepthShader(),
        &ActorDepthShader(),
        &DepthRenderer(),
        &LineShader(),
        &RingShader(),
        &CircleShader(),
        &CircleMaskShader(),
        &RectangleShader(),
        &PlainColorShader(),
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


void BaseShaderCode::AddShaders(ManagedArray<const ShaderSource*>& shaderSource) {
    for (const ShaderSource* source : shaderSource) {
        Shader* shader = new Shader(source->m_name);
        if (shader->Create(source->m_vs, source->m_fs)) 
            m_shaders[source->m_name] = shader;
#ifdef _DEBUG
        else
            fprintf(stderr, "creating shader '%s' failed\n", (const char*) source->m_name);
#endif
    }
}

// =================================================================================================
