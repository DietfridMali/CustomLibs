#pragma once
#include <utility>

#include <stdlib.h>
#include <math.h>
#include "singletonbase.hpp"
#include "array.hpp"
#include "fbo.h"

// =================================================================================================

class DrawBufferInfo {
public:
    FBO*                    m_fbo;
    ManagedArray<GLuint>*   m_drawBuffers;

public:
    DrawBufferInfo(FBO* fbo = nullptr, ManagedArray<GLuint>* drawBuffers = nullptr) {
        Update(fbo, drawBuffers);
    }

    inline void Update (FBO* fbo, ManagedArray<GLuint>* drawBuffers) {
        m_fbo = fbo;
        m_drawBuffers = drawBuffers;
    }
};

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view matrix

class DrawBufferHandler
{
    protected:
        FBO*                    m_activeBuffer;
        ManagedArray<GLuint>    m_defaultDrawBuffers;
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

        bool SetActiveBuffer(FBO* buffer, bool clearBuffer = false);

        inline ManagedArray<GLuint>* ActiveDrawBuffers(void) {
            return m_drawBufferInfo.m_drawBuffers;
        }

        void SetupDrawBuffers(void);
            
        void SetActiveDrawBuffers(void);

        void SaveDrawBuffer();

        void SetDrawBuffers(FBO* fbo, ManagedArray<GLuint>* drawBuffers);

        void RestoreDrawBuffer(void);

        void ResetDrawBuffers(FBO* activeBuffer, bool clearBuffer = true);
};

// =================================================================================================
