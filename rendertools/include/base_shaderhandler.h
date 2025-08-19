#pragma once

#include "shader.h"
#include "base_shadercode.h"
#include "matrix.hpp"
#include "singletonbase.hpp"

// =================================================================================================

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
        : m_kernels(16), m_shaderCode(nullptr), m_activeShader (nullptr), m_activeShaderId("")
    {
#if 0
        List<String> filenames = { appData->textureFolder + "graynoise.png" };
        m_grayNoise.CreateFromFile(filenames, appData->flipImagesVertically);
#endif
        ComputeGaussKernels();
    }

    virtual ~BaseShaderHandler() {
        if (m_shaderCode)
            delete m_shaderCode; // Speicherbereinigung nicht vergessen!
    }

    static BaseShaderHandler& Instance(void) { return dynamic_cast<BaseShaderHandler&>(PolymorphSingleton::Instance()); }

protected:
    virtual void CreateShaderCode(void) {  m_shaderCode = new BaseShaderCode(); }

public:
    void CreateShaders(void) {
        if (m_shaderCode == nullptr)
            CreateShaderCode();
    }

    Shader* SelectShader(Texture* texture);

    Shader* SetupShader(String shaderId);

    void StopShader(bool needLegacyMatrices = false);

    inline bool ShaderIsActive(Shader* shader = nullptr) {
        return m_activeShader != shader;
    }

    inline Shader* GetShader(String shaderId) {
        return m_shaderCode->GetShader (shaderId);
    }

    inline FloatArray* GetKernel(int radius) {
        return ((radius < 1) or (radius > m_kernels.Length())) ? nullptr : m_kernels[radius - 1];
    }

private:
    FloatArray* ComputeGaussKernel1D(int radius);

    void ComputeGaussKernels(void);
};

#define baseShaderHandler BaseShaderHandler::Instance()

// =================================================================================================

