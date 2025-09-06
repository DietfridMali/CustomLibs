
#include "matrix.hpp"
#include "base_shaderhandler.h"
#include "base_renderer.h"

#define _USE_MATH_DEFINES
#include <math.h>

// =================================================================================================

FloatArray* BaseShaderHandler::ComputeGaussKernel1D(int radius) {
    FloatArray* kernel = new FloatArray(2 * radius + 1);

    const float sigma = float(radius) / 1.6f; // 2.0f; // Standardabweichung
    const float sigma2 = 2.0f * sigma * sigma;
    const float sqrtSigmaPi2 = float (std::sqrt(M_PI * sigma2));
    float sum = 0.0f;
#ifdef _DEBUG
    float k[33];
#endif

    for (int i = -radius; i <= radius; ++i) {
        float value = std::exp(-i * i / sigma2) / sqrtSigmaPi2;
        (*kernel)[i + radius] = value;
#ifdef _DEBUG
        k[i + radius] = value;
#endif
        sum += value;
    }

    // normalisation
    for (auto& value : *kernel)
        value /= sum;
#ifdef _DEBUG
    for (int i = 0; i < 2 * radius + 1; i++)
        k[i] /= sum;
#endif
    return kernel;
}


void BaseShaderHandler::ComputeGaussKernels(void) {
    for (int radius = 1; radius <= 16; radius++)
        m_kernels[radius - 1] = ComputeGaussKernel1D(radius);
}


Shader* BaseShaderHandler::SelectShader(Texture* texture) {
    String shaderId = "";
    if (not texture)
        shaderId = "color";
    // select shader depending on texture type
    else if (texture->Type() == GL_TEXTURE_CUBE_MAP)
        shaderId = "cubemap";
    else if (texture->Type() == GL_TEXTURE_2D)
        shaderId = "texture";
    else
        return nullptr;
    return SetupShader(shaderId);
}


Shader* BaseShaderHandler::SetupShader(String shaderId) {
    Shader* shader;
    if (baseRenderer.DepthPass())
        shaderId = "depthShader"; // override all shaders with simplest possible shader during depth pass
    if ((m_activeShaderId == shaderId) and (m_activeShader != nullptr))
        shader = m_activeShader;
    else {
        shader = GetShader(shaderId);
        //Shader** shaderPtr = m_shaders.Find(shaderId); // m_shaders[shaderId];
        if (shader == nullptr) {
            //fprintf(stderr, "*** couldn't find shader'%s'\r\n", (char*)shaderId);
            return nullptr;
        }
        //Shader* shader = *shaderPtr;
        if (shader->m_handle == 0) {
            fprintf(stderr, "*** shader'%s' is not available\r\n", (char*)shaderId);
            return nullptr;
        }
        //fprintf(stderr, "loading shader '%s'\r\n", (char*) shaderId);
        m_activeShader = shader;
        m_activeShaderId = shaderId;
        shader->Enable();
    }
    shader->UpdateMatrices();
    return baseRenderer.DepthPass() ? nullptr : shader; // pretend no shader was loaded during depth pass so the app doesn't try to set uniforms
}


void BaseShaderHandler::StopShader(bool needLegacyMatrices) {
    if (ShaderIsActive()) {
        m_activeShader->Disable();
        m_activeShader = nullptr;
        m_activeShaderId = "";
#if 1
        if (needLegacyMatrices)
            baseRenderer.UpdateLegacyMatrices();
#endif
    }
}

Shader* BaseShaderHandler::LoadPlainColorShader(const RGBAColor& color, bool premultiply) {
    Shader* shader = SetupShader("plainColor");
    if (shader and not baseRenderer.DepthPass()) {
        static ShaderLocationTable locations;
        locations.Start();
        shader->SetVector4f("surfaceColor", locations.Current(), premultiply ? color.Premultiplied() : color);
    }
    return shader;
}


Shader* BaseShaderHandler::LoadPlainTextureShader(const RGBAColor& color, const Vector2f& tcOffset, const Vector2f& tcScale, bool premultiply) {
    Shader* shader = SetupShader("plainTexture");
    if (shader and not baseRenderer.DepthPass()) {
        static ShaderLocationTable locations;
        locations.Start();
        shader->SetVector4f("surfaceColor", locations.Current(), color);
        shader->SetVector2f("tcOffset", locations.Current(), tcOffset);
        shader->SetVector2f("tcScale", locations.Current(), tcScale);
        //shader->SetFloat("premultiply", locations.Current(), premultiply ? 1.0f : 0.0f);
    }
    return shader;
}


Shader* BaseShaderHandler::LoadBlurTextureShader(Vector2f viewportSize, const RGBAColor& color, const GaussBlurParams& blur, bool premultiply) {
    Shader* shader = SetupShader("blurTexture");
    if (shader and not baseRenderer.DepthPass()) {
        static ShaderLocationTable locations;
        locations.Start();
        shader->SetVector4f("surfaceColor", locations.Current(), color);
        SetGaussBlurParams(shader, viewportSize, blur);
        //shader->SetFloat("premultiply", locations.Current(), premultiply ? 1.0f : 0.0f);
    }
    return shader;
}


Shader* BaseShaderHandler::LoadGrayscaleShader(float brightness, const Vector2f& tcOffset, const Vector2f& tcScale) {
    Shader* shader = SetupShader("grayScale");
    if (shader and not baseRenderer.DepthPass()) {
        ShaderLocationTable locations;
        locations.Start();
        shader->SetVector2f("tcOffset", locations.Current(), tcOffset);
        shader->SetVector2f("tcScale", locations.Current(), tcScale);
        shader->SetFloat("brightness", locations.Current(), brightness);
    }
    return shader;
}


Shader* BaseShaderHandler::SetGaussBlurParams(Shader* shader, Vector2f viewportSize, const GaussBlurParams& blur) {
    if (shader and not baseRenderer.DepthPass()) {
        static ShaderLocationTable locations;
        locations.Start();
        shader->SetVector2f("texelSize", locations.Current(), Vector2f(1.0f / viewportSize.X(), 1.0f / viewportSize.Y()));
        shader->SetInt("blurStrength", locations.Current(), blur.strength);
        shader->SetFloat("blurSpread", locations.Current(), blur.spread);
        return shader;
    }
    return nullptr;
}


Shader* BaseShaderHandler::SetChromAbParams(Shader* shader, float aberration, int offsetType) { // offsetType: 0 - linear, 1 - radial
    if (shader and not baseRenderer.DepthPass()) {
        static ShaderLocationTable locations;
        locations.Start();
        shader->SetInt("offsetType", locations.Current(), offsetType);
        shader->SetFloat("aberration", locations.Current(), aberration);
        return shader;
    }
    return nullptr;
}


// =================================================================================================
