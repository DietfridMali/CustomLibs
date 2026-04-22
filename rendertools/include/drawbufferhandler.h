#pragma once

#include <math.h>
#include <utility>
#include <stdlib.h>

#include "std_defines.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "rendertarget.h"

// =================================================================================================

class DrawBufferInfo {
public:
    RenderTarget*       m_renderTarget;
#ifdef OPENGL
    AutoArray<GLuint>*  m_drawBuffers;
#endif

public:
#ifdef OPENGL
    DrawBufferInfo(RenderTarget* renderTarget = nullptr, AutoArray<GLuint>* drawBuffers = nullptr) {
        Update(renderTarget, drawBuffers);
    }

    inline void Update (RenderTarget* renderTarget, AutoArray<GLuint>* drawBuffers) {
        m_renderTarget = renderTarget;
        m_drawBuffers = drawBuffers;
    }
#else
    DrawBufferInfo(RenderTarget* renderTarget = nullptr) : m_renderTarget(renderTarget) {}
#endif

    bool operator==(const DrawBufferInfo& other) const noexcept {
        return other.m_renderTarget == m_renderTarget;
    }
};

// =================================================================================================

class DrawBufferHandler
{
    protected:
        RenderTarget*       m_activeBuffer;
        RenderTarget*       m_parentBuffer;
#ifdef OPENGL
        AutoArray<GLuint>   m_defaultDrawBuffers;
#endif
        List<RenderTarget*> m_drawBufferStack;
        int                 m_windowWidth;
        int                 m_windowHeight;

    public:
        DrawBufferHandler()
            : m_activeBuffer(nullptr)
            , m_parentBuffer(nullptr)
        { }

        void Setup(int windowWidth, int windowHeight);

        void ActivateDrawBuffer(RenderTarget* buffer);

        bool DeactivateDrawBuffer(RenderTarget* buffer);

        inline AutoArray<GLuint>* ActiveDrawBuffers(void) {
            return m_activeBuffer ? &m_activeBuffer->DrawBuffers() : nullptr;
        }

        void ResetDrawBuffers(void);

        void SetActiveDrawBuffers(void);
};

// =================================================================================================
