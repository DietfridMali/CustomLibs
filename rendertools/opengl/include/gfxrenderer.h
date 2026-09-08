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

    virtual bool StartOperation(void** cl, String name, bool piggyback = true) noexcept override {
        return BaseRenderer::StartOperation(cl, name, piggyback);
    }

    virtual bool FinishOperation(void* cl, bool flush = false) noexcept override;

    inline void Draw3DScene(void) noexcept {
        return BaseRenderer::Draw3DScene(true);
    }

    virtual void DrawScreen(bool bRotate, bool bFlipVertically) override;

    // The presented picture as packed RGBA8, bottom row first: the window's back buffer, what
    // DrawScreen () just put there. width/height of 0 mean the whole back buffer; a rectangle is
    // (x, y) from the bottom left, and the destination needs width * height * 4 bytes. The same
    // call exists in every backend - here it is a glReadPixels of the default framebuffer, DX and
    // VK copy the swap chain image through a readback buffer.
    bool ReadBuffer(void* buffer, size_t bufferSize, int x = 0, int y = 0, int width = 0, int height = 0);

    inline void SetGeometryFrontFace(void) noexcept {
        gfxStates.FrontFace(GfxOperations::Winding::Reverse);
    }

    inline void SetShadowFrontFace(void) noexcept {
        gfxStates.FrontFace(GfxOperations::Winding::Regular);
    }
};

#define baseRenderer GfxRenderer::Instance()

// =================================================================================================
