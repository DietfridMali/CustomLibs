#pragma once

#include <math.h>
#include <utility>
#include <stdlib.h>

#include "dx12framework.h"
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
    , public DrawBufferHandler
{
public:
    virtual ~GfxRenderer() {
    }

    GfxRenderer() 
        : BaseRenderer()
    {
        _instance = this;
    }

    static GfxRenderer& Instance(void) {
        return dynamic_cast<GfxRenderer&>(PolymorphSingleton::Instance());
    }

    virtual bool InitGraphics(void) override;

    virtual void Init(int width, int height, float fov, float zNear, float zFar) override;

    virtual void* StartOperation(String name) noexcept override;

    virtual bool FinishOperation(void* cl, bool flush = false) noexcept override;

    virtual RenderTarget* GetActiveBuffer(void) noexcept override { 
        return m_activeBuffer; 
    }

    virtual void ResetDrawBuffers(void) noexcept override {
        DrawBufferHandler::ResetDrawBuffers();
    }





#include "gfxapitype.h"

};

#define baseRenderer GfxRenderer::Instance()

// =================================================================================================
