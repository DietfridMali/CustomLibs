#pragma once

#include <math.h>
#include <utility>
#include <stdlib.h>

#include "glew.h"
#include "std_defines.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "matrix.hpp"
#include "projector.h"
#include "rendermatrices.h"
#include "viewport.h"
#include "rendertarget.h"
#include "drawbufferhandler.h"
#include "framecounter.h"
#include "base_renderer.h"

// =================================================================================================
// DX12 Renderer
//
// Manages the 3D scene render targets, coordinate transforms, viewports, and the render loop.
// "OpenGL" terminology is preserved in function names for source compatibility with the game layer
// (SetupOpenGL → SetupDX12 internally, but callers still see SetupOpenGL for now).

class GfxRenderer
    : public BaseRenderer
{
public:
    struct GLVersion {
        GLint major{ 0 };
        GLint minor{ 0 };
    };

    GLVersion   m_glVersion;

    virtual ~GfxRenderer() {
    }

    GfxRenderer() 
        : BaseRenderer()
    {
        _instance = this;
        gfxApiType = BaseRenderer::GfxApiType::OpenGL;
    }

    static GfxRenderer& Instance(void) {
        return dynamic_cast<GfxRenderer&>(PolymorphSingleton::Instance());
    }

    virtual bool InitGraphics(void) override;

    virtual void* StartOperation(String name, bool piggyback = true) noexcept override;

    virtual bool FinishOperation(void* cl, bool flush = false) noexcept override;

    inline void Draw3DScene(void) noexcept {
        return BaseRenderer::Draw3DScene(true);
    }

    virtual void DrawScreen(bool bRotate, bool bFlipVertically) override;

    inline void SetGeometryFrontFace(void) noexcept {
        gfxStates.FrontFace(GfxOperations::Winding::Reverse);
    }

    inline void SetShadowFrontFace(void) noexcept {
        gfxStates.FrontFace(GfxOperations::Winding::Regular);
    }
};

#define baseRenderer GfxRenderer::Instance()

// =================================================================================================
