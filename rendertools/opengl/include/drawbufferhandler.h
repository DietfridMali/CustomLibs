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
    AutoArray<GLuint>*   m_drawBuffers;

public:
    DrawBufferInfo(RenderTarget* renderTarget = nullptr, AutoArray<GLuint>* drawBuffers = nullptr) {
        Update(renderTarget, drawBuffers);
    }

    inline void Update (RenderTarget* renderTarget, AutoArray<GLuint>* drawBuffers) {
        m_renderTarget = renderTarget;
        m_drawBuffers = drawBuffers;
    }

    bool operator==(const DrawBufferInfo& other) const {
        return other.m_renderTarget == m_renderTarget;
    }
};

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view matrix

class DrawBufferHandler
{
    protected:
        RenderTarget*                    m_activeBuffer;
        AutoArray<GLuint>    m_defaultDrawBuffers;
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

        inline AutoArray<GLuint>* ActiveDrawBuffers(void) {
            return m_drawBufferInfo.m_drawBuffers;
        }

        void SetupDrawBuffers(void);
            
        void SetActiveDrawBuffers(void);

        void SaveDrawBuffer();

        void SetDrawBuffers(RenderTarget* renderTarget, AutoArray<GLuint>* drawBuffers);

        void RestoreDrawBuffer(void);

        void RemoveDrawBuffer(RenderTarget* buffer);

        void ResetDrawBuffers(RenderTarget* activeBuffer, bool clearBuffer = true);
};

// =================================================================================================
