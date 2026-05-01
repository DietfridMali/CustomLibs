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
{
private:
    CommandList*    m_cmdList{ nullptr };
    CommandList*    m_temporaryList{ nullptr };

protected:
    RenderStates    m_renderStates;

public:
    virtual ~GfxRenderer() {
    }

    GfxRenderer() 
        : BaseRenderer()
    {
        _instance = this;
        m_frontFace = GfxOperations::CullFace::Front;
        m_backFace = GfxOperations::CullFace::Back;
        gfxApiType = BaseRenderer::GfxApiType::DirectX;

    }

    static GfxRenderer& Instance(void) {
        return dynamic_cast<GfxRenderer&>(PolymorphSingleton::Instance());
    }

    virtual bool InitGraphics(void) override;

    virtual void* StartOperation(String name, bool piggyback = true) noexcept override;

    virtual bool FinishOperation(void* cl, bool flush = false) noexcept override;

    virtual void DrawScreen(bool bRotate, bool bFlipVertically) override;

    inline void Draw3DScene(void) noexcept {
        return BaseRenderer::Draw3DScene(false);
    }

    inline ::RenderStates& RenderStates(void) noexcept {
        return m_renderStates;
    }

    inline void SetGeometryFrontFace(void) noexcept {
        gfxStates.FrontFace(GfxOperations::CullFace::Back);
    }

    inline void SetShadowFrontFace(void) noexcept {
        gfxStates.FrontFace(GfxOperations::CullFace::Front);
    }
};

#define baseRenderer GfxRenderer::Instance()

// =================================================================================================
