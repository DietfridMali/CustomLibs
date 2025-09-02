#include "base_renderer.h"
#include "base_shaderhandler.h"
#include "outlinerenderer.h"
#include "base_renderer.h"

// =================================================================================================
// the(text) outline renderer works as follows :
// it uses render - to - texture with an FBO
// the FBO has three color buffers
// the text goes to the first color buffer.it must be sized down so that there is enough room around it for the outline
// the width of which is defined by the outlineWidth parameter
// the outline renderer uses buffer[0] as source and renders it to buffer[1], applying the outline shader in horizontal direction
// (i.e. the resulting text is widened horizontally)
// then buffer[1] is used as source and rendered to buffer[2], applying the vertical outline shader
// now buffer[2] contains a "fatter" version of the original text(buffer[0])
// render buffer[2] in the desired outline color and then buffer[2] on top of it : Voilà, there is your text with an outline


#define AUTORENDER 0

void OutlineRenderer::AntiAlias(FBO* fbo, const AAMethod& aaMethod, bool premultiply) {
    static ShaderLocationTable locations;
    if (aaMethod.ApplyAA()) {
        FBO::FBORenderParams params = { .clearBuffer = true, .scale = 1.0f };
        params.shader = baseShaderHandler.SetupShader(aaMethod.method);
        if (params.shader == nullptr)
            return;
        BaseRenderer::ClearGLError();
        locations.Start();
        params.shader->SetFloat("offset", locations.Current(), 0.5f);
        //params.shader->SetFloat("premultiply", locations.Current(), premultiply ? 1.0f : 0.0f);
        if (aaMethod.method != "gaussblur")
            fbo->AutoRender(params);
        else {
            FloatArray* kernel = baseShaderHandler.GetKernel(aaMethod.strength);
            if (kernel != nullptr) {
                params.shader->SetFloatData("coeffs", locations.Current(), *kernel);
                params.shader->SetInt("radius", locations.Current(), aaMethod.strength);
                params.destination = fbo->GetLastDestination();
                
                for (int i = 0; i < 2; ++i) {
                    // the following code only works if not called multiple times in a loop!
                    params.shader->SetFloat("direction", locations.Current(), float (i));
                    params.source = params.destination;
                    params.destination = fbo->NextBuffer(params.source);
                    fbo->Render(params);
                }
            }
        }
        //baseShaderHandler.StopShader();
    }
}


void OutlineRenderer::RenderOutline(FBO* fbo, const Decoration& decoration, bool premultiply) {
    if (decoration.HaveOutline()) {
        Shader* shader = baseShaderHandler.SetupShader("outline");
        if (shader and not baseRenderer.DepthPass()) {
            static ShaderLocationTable locations;
            locations.Start();
            shader->SetFloat("outlineWidth", locations.Current(), decoration.outlineWidth);
            shader->SetVector4f("outlineColor", locations.Current(), decoration.outlineColor);
            shader->SetFloat("offset", locations.Current(), 0.5f);
            //shader->SetFloat("premultiply", locations.Current(), premultiply ? 1.0f : 0.0f);
            fbo->AutoRender({ .clearBuffer = true, .shader = shader });
        }
        //baseShaderHandler.StopShader();
        AntiAlias(fbo, decoration.aaMethod);
    }
}

// =================================================================================================
