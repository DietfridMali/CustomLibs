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
    public:
        DrawBufferHandler()
            : m_activeBuffer(nullptr)
        { 
        }

        bool SetActiveBuffer(FBO* buffer, bool clearBuffer = false);

        inline ManagedArray<GLuint>* ActiveDrawBuffers(void) {
            return m_drawBufferInfo.m_drawBuffers;
        }

        void SetupDrawBuffers(void);
            
        inline void SetActiveDrawBuffers(void) {
            glDrawBuffers(ActiveDrawBuffers()->Length(), ActiveDrawBuffers()->Data());
        }

        void SaveDrawBuffer();

        void SetDrawBuffers(FBO* fbo, ManagedArray<GLuint>* drawBuffers);

        void RestoreDrawBuffer(void);

        void ResetDrawBuffers(FBO* activeBuffer, bool clearBuffer = true);
};

// =================================================================================================
