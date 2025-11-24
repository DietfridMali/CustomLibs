
#include "matrix.hpp"
#include "base_shaderhandler.h"
#include "base_renderer.h"
#include "conversions.hpp"

#define _USE_MATH_DEFINES
#include <math.h>

// =================================================================================================

FloatArray* BaseShaderHandler::ComputeGaussKernel1D(int radius) {
    FloatArray* kernel = new FloatArray(2 * radius + 1);

    const float sigma = float(radius) / 1.6f; // 2.0f; // Standardabweichung
    const float sigma2 = 2.0f * sigma * sigma;
    const float sqrtSigmaPi2 = float (std::sqrt(PI * sigma2));
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


Shader* BaseShaderHandler::SetupShader(String shaderId, String depthShaderId) {
    Shader* shader;
    if (baseRenderer.IsShadowPass())
        shaderId = depthShaderId; // override all shaders with simplest possible shader during depth pass
    if ((m_activeShaderId == shaderId) and (m_activeShader != nullptr))
        shader = m_activeShader;
    else {
        shader = GetShader(shaderId);
        if (shader == nullptr)
            return nullptr;
        if (shader->m_handle == 0) {
#ifdef _DEBUG
            fprintf(stderr, "*** shader'%s' is not available\r\n", (char*)shaderId);
#endif
            return nullptr;
        }
        m_activeShader = shader;
        m_activeShaderId = shaderId;
        shader->Enable();
    }
    shader->UpdateMatrices();
    return /*baseRenderer.IsShadowPass() ? nullptr : */ shader; // pretend no shader was loaded during depth pass so the app doesn't try to set uniforms
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


Shader* BaseShaderHandler::LoadLineShader(const RGBAColor& color, const Vector2f& start, const Vector2f& end, float strength, bool antialias) {
    Shader* shader = SetupShader("lineShader");
    if (shader) {
        shader->SetVector4f("surfaceColor", color);
        if (not baseRenderer.IsShadowPass()) {
            shader->SetVector2f("viewportSize", baseRenderer.ViewportSize());
            shader->SetVector2f("start", start);
            shader->SetVector2f("end", end);
            shader->SetFloat("strength", strength);
            shader->SetInt("antialias", antialias ? 1 : 0);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::LoadRingShader(const RGBAColor& color, const Vector2f& center, float radius, float strength, float startAngle, float endAngle, bool antialias) {
    Shader* shader = SetupShader("ringShader");
    if (shader) {
        shader->SetVector4f("surfaceColor", color);
        if (not baseRenderer.IsShadowPass()) {
            shader->SetVector2f("viewportSize", baseRenderer.ViewportSize());
            shader->SetVector2f("center", center);
            shader->SetFloat("radius", radius);
            shader->SetFloat("strength", strength);
            shader->SetFloat("startAngle", startAngle);
            shader->SetFloat("endAngle", endAngle);
            shader->SetInt("antialias", antialias ? 1 : 0);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::LoadCircleShader(const RGBAColor& color, const Vector2f& center, float radius, bool antialias) {
    Shader* shader = SetupShader("circleShader");
    if (shader) {
        shader->SetVector4f("surfaceColor", color);
        if (not baseRenderer.IsShadowPass()) {
            shader->SetVector2f("viewportSize", baseRenderer.ViewportSize());
            shader->SetVector2f("center", center);
            shader->SetFloat("radius", radius);
            shader->SetInt("antialias", antialias ? 1 : 0);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::LoadRectangleShader(const RGBAColor& color, const Vector2f& center, float width, float height, float strength, float radius, bool antialias) {
    Shader* shader = SetupShader("rectangleShader");
    if (shader) {
        shader->SetVector4f("surfaceColor", color);
        if (not baseRenderer.IsShadowPass()) {
            shader->SetVector2f("viewportSize", baseRenderer.ViewportSize());
            shader->SetVector2f("center", center);
            shader->SetVector2f("size", Vector2f(width, height));
            shader->SetFloat("strength", strength);
            shader->SetFloat("radius", radius);
            shader->SetInt("antialias", antialias ? 1 : 0);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::LoadCircleMaskShader(const RGBAColor& color, const RGBAColor& maskColor, const Vector2f& center, float radius, float maskScale, bool antialias) {
    Shader* shader = SetupShader("circleMaskShader");
    if (shader) {
        shader->SetInt("surface", 0);
        shader->SetVector4f("surfaceColor", color);
        if (not baseRenderer.IsShadowPass()) {
            shader->SetVector2f("viewportSize", baseRenderer.ViewportSize());
            shader->SetVector4f("maskColor", maskColor);
            shader->SetVector2f("center", center);
            shader->SetFloat("radius", radius);
            shader->SetFloat("maskScale", maskScale);
            shader->SetInt("antialias", antialias ? 1 : 0);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::LoadPlainColorShader(const RGBAColor& color, bool premultiply) {
    Shader* shader = SetupShader("plainColor");
    if (shader) {
        shader->SetVector4f("surfaceColor", premultiply ? color.Premultiplied() : color);
    }
    return shader;
}


Shader* BaseShaderHandler::LoadPlainTextureShader(const RGBAColor& color, const Vector2f& tcOffset, const Vector2f& tcScale, bool premultiply) {
    Shader* shader = SetupShader("plainTexture");
    if (shader) {
        shader->SetInt("surface", 0);
        shader->SetVector4f("surfaceColor", color);
        if (not baseRenderer.IsShadowPass()) {
            shader->SetVector2f("tcOffset", tcOffset);
            shader->SetVector2f("tcScale", tcScale);
            //shader->SetInt("surface", 0);
            //shader->SetFloat("premultiply", premultiply ? 1.0f : 0.0f);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::LoadBlurTextureShader(const RGBAColor& color, const GaussBlurParams& blur, bool premultiply) {
    Shader* shader = SetupShader("blurTexture");
    if (shader) {
        shader->SetInt("surface", 0);
        shader->SetVector4f("surfaceColor", color);
        if (not baseRenderer.IsShadowPass()) {
            SetGaussBlurParams(shader, blur);
        //shader->SetFloat("premultiply", premultiply ? 1.0f : 0.0f);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::LoadGrayscaleShader(float brightness, const Vector2f& tcOffset, const Vector2f& tcScale) {
    Shader* shader = SetupShader("grayScale");
    if (shader) {
        if (baseRenderer.IsShadowPass())
            shader->SetVector4f("surfaceColor", ColorData::White);
        else {
            shader->SetInt("surface", 0);
            shader->SetVector2f("tcOffset", tcOffset);
            shader->SetVector2f("tcScale", tcScale);
            shader->SetFloat("brightness", brightness);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::SetGaussBlurParams(Shader* shader, const GaussBlurParams& blur) {
    if (shader) {
        if (baseRenderer.IsShadowPass())
            shader->SetVector4f("surfaceColor", ColorData::White);
        else {
            shader->SetVector2f("texelSize", baseRenderer.TexelSize());
            shader->SetInt("blurStrength", blur.strength);
            shader->SetFloat("blurSpread", blur.spread);
        }
    }
    return shader;
}


Shader* BaseShaderHandler::SetChromAbParams(Shader* shader, float aberration, int offsetType) { // offsetType: 0 - linear, 1 - radial
    if (shader) {
        if (baseRenderer.IsShadowPass())
            shader->SetVector4f("surfaceColor", ColorData::White);
        else {
            shader->SetInt("offsetType", offsetType);
            shader->SetFloat("aberration", aberration);
        }
    }
    return shader;
}

// =================================================================================================
