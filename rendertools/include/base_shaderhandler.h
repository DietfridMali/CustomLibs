#pragma once

#include "texture.h"
#include "shader.h"
#include "texcoord.h"
#include "base_shadercode.h"
#include "matrix.hpp"
#include "singletonbase.hpp"

// =================================================================================================

struct GaussBlurParams {
    int strength = 3;
    float spread = 3.0f;
};

class BaseShaderHandler
    : public PolymorphSingleton<BaseShaderHandler>
{
public:
    typedef Shader* (__cdecl* tShaderLoader) (void);

    ManagedArray<FloatArray*>   m_kernels;
    Shader*                     m_activeShader;
    String                      m_activeShaderId;
    Texture                     m_grayNoise;
    BaseShaderCode*             m_shaderCode;

    BaseShaderHandler()
        : m_kernels(16), m_shaderCode(nullptr), m_activeShader(nullptr), m_activeShaderId("")
    {
#if 0
        List<String> filenames = { appData->textureFolder + "graynoise.png" };
        m_grayNoise.CreateFromFile(filenames, false, appData->flipImagesVertically);
#endif
        ComputeGaussKernels(); // kann allozieren -> nicht noexcept markieren
    }

    virtual ~BaseShaderHandler() noexcept {
        if (m_shaderCode)
            delete m_shaderCode;
    }

    static BaseShaderHandler& Instance(void) { return dynamic_cast<BaseShaderHandler&>(PolymorphSingleton::Instance()); }

protected:
    virtual void CreateShaderCode(void) { m_shaderCode = new BaseShaderCode(); } // allokiert -> nicht noexcept

public:
    void CreateShaders(void) {
        if (m_shaderCode == nullptr)
            CreateShaderCode();
    }

    Shader* SelectShader(Texture* texture);
    Shader* SetupShader(String shaderId);
    void StopShader(bool needLegacyMatrices = false);

    inline bool ShaderIsActive(Shader* shader = nullptr) const noexcept {
        return m_activeShader != shader;
    }

    inline Shader* GetShader(String shaderId) {
        return m_shaderCode->GetShader(shaderId);
    }

    inline FloatArray* GetKernel(int radius) noexcept {
        return ((radius < 1) or (radius > m_kernels.Length())) ? nullptr : m_kernels[radius - 1];
    }

    Shader* LoadLineShader(const RGBAColor& color, const Vector2f& start, const Vector2f& end, float strength, bool antialias);

    inline Shader* LoadLineShader(RGBAColor&& color, Vector2f&& start, Vector2f&& end, float strength, bool antialias) {
        return LoadLineShader(static_cast<const RGBAColor&>(color), static_cast<const Vector2f&>(start), static_cast<const Vector2f&>(end), strength, antialias);
    }

    Shader* LoadRingShader(const RGBAColor& color, const Vector2f& center, float radius, float strength, bool antialias);

    Shader* LoadRingShader(RGBAColor&& color, Vector2f&& center, float radius, float strength, bool antialias) {
        return LoadRingShader(static_cast<const RGBAColor&>(color), static_cast<const Vector2f&>(center), radius, strength, antialias);
    }

    Shader* LoadCircleShader(const RGBAColor& color, const Vector2f& center, float radius, bool antialias);

    Shader* LoadCircleShader(RGBAColor&& color, Vector2f&& center, float radius, bool antialias) {
        return LoadCircleShader(static_cast<const RGBAColor&>(color), static_cast<const Vector2f&>(center), radius, antialias);
    }

    Shader* LoadCircleMaskShader(const RGBAColor& color, const RGBAColor& maskColor, const Vector2f& center, float radius, float maskScale = 1.0f, bool antialias = true);

    Shader* LoadCircleMaskShader(RGBAColor&& color, RGBAColor&& maskColor, Vector2f&& center, float radius, float maskScale = 1.0f, bool antialias = true) {
        return LoadCircleMaskShader(static_cast<const RGBAColor&>(color), static_cast<const RGBAColor&>(maskColor), static_cast<const Vector2f&>(center), radius, maskScale, antialias);
    }

    Shader* LoadPlainColorShader(const RGBAColor& color, bool premultiply = false);

    Shader* LoadPlainTextureShader(const RGBAColor& color, const Vector2f& tcOffset = Vector2f::ZERO, const Vector2f& tcScale = Vector2f::ONE, bool premultiply = false);

    Shader* LoadBlurTextureShader(const RGBAColor& color, const GaussBlurParams& params = {}, bool premultiply = false);

    Shader* LoadGrayscaleShader(float brightness, const Vector2f& tcOffset = Vector2f::ZERO, const Vector2f& tcScale = Vector2f::ONE);

    Shader* SetGaussBlurParams(Shader* shader, const GaussBlurParams& params = {});

    Shader* SetChromAbParams(Shader* shader, float aberration = 0.1f, int offsetType = 1);

private:
    FloatArray* ComputeGaussKernel1D(int radius); // allokiert -> nicht noexcept
    void ComputeGaussKernels(void);               // allokiert -> nicht noexcept
};

#define baseShaderHandler BaseShaderHandler::Instance()

// =================================================================================================
