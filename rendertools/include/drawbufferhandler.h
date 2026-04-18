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
    RenderTarget*                    m_renderTarget;
#ifdef OPENGL
    AutoArray<GLuint>*   m_drawBuffers;
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
        RenderTarget*                    m_activeBuffer;
#ifdef OPENGL
        AutoArray<GLuint>    m_defaultDrawBuffers;
#endif
        DrawBufferInfo          m_drawBufferInfo;
        List<DrawBufferInfo>    m_drawBufferStack;
        int                     m_windowWidth;
        int                     m_windowHeight;
    public:
        DrawBufferHandler()
            : m_activeBuffer(nullptr), m_windowWidth(0), m_windowHeight(0)
        { }

        void Setup(int windowWidth, int windowHeight) {
            m_windowWidth = windowWidth;
            m_windowHeight = windowHeight;
        }

        bool SetActiveBuffer(RenderTarget* buffer, bool clearBuffer = false);

#ifdef OPENGL
        inline AutoArray<GLuint>* ActiveDrawBuffers(void) {
            return m_drawBufferInfo.m_drawBuffers;
        }
#endif

        void SetupDrawBuffers(void);

        void SetActiveDrawBuffers(void);

        void SaveDrawBuffer();

#ifdef OPENGL
        void TrackDrawBuffers(RenderTarget* renderTarget, AutoArray<GLuint>* drawBuffers);
#else
        void TrackDrawBuffers(RenderTarget* renderTarget);
#endif

        void RestoreDrawBuffer(void);

        void RemoveDrawBuffer(RenderTarget* buffer);

        void ResetDrawBuffers(RenderTarget* activeBuffer, bool clearBuffer = true);
};

// =================================================================================================
