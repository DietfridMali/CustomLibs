#pragma once

#include <math.h>
#include <utility>
#include <stdlib.h>

#include "framework.h"
#include "std_defines.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "fbo.h"

// =================================================================================================
// DX12 DrawBufferHandler
//
// In OpenGL, multiple color attachments were listed in a GLuint DrawBufferList and passed to
// glDrawBuffers(). In DX12, OMSetRenderTargets accepts an array of D3D12_CPU_DESCRIPTOR_HANDLEs
// (RTVs) and optionally a DSV handle. The FBO class (see fbo.h) owns these handles.
//
// This class manages the active render target (FBO), the stack for save/restore, and delegates
// the actual OMSetRenderTargets calls to the active FBO.

class DrawBufferInfo {
public:
    FBO* m_fbo{ nullptr };

    DrawBufferInfo(FBO* fbo = nullptr) : m_fbo(fbo) {}

    bool operator==(const DrawBufferInfo& other) const noexcept {
        return other.m_fbo == m_fbo;
    }
};

// =================================================================================================

class DrawBufferHandler
{
protected:
    FBO*                    m_activeBuffer{ nullptr };
    DrawBufferInfo          m_drawBufferInfo;
    List<DrawBufferInfo>    m_drawBufferStack;
    int                     m_windowWidth{ 0 };
    int                     m_windowHeight{ 0 };

public:
    DrawBufferHandler() = default;

    void Setup(int windowWidth, int windowHeight) noexcept {
        m_windowWidth  = windowWidth;
        m_windowHeight = windowHeight;
    }

    // Activates buffer as the current render target.
    // If clearBuffer is true, the buffer's color and depth are cleared.
    bool SetActiveBuffer(FBO* buffer, bool clearBuffer = false);

    // Establishes the default draw buffer set (called once on renderer creation).
    void SetupDrawBuffers(void);

    // Calls OMSetRenderTargets for the currently tracked draw buffer set.
    void SetActiveDrawBuffers(void);

    // Pushes the current draw buffer state onto the save stack.
    void SaveDrawBuffer(void);

    // Sets fbo as the active draw buffer (without the save/restore stack).
    void SetDrawBuffers(FBO* fbo);

    // Pops the top draw buffer state from the stack and restores it.
    void RestoreDrawBuffer(void);

    // Removes any reference to buffer from the stack and active state.
    void RemoveDrawBuffer(FBO* buffer);

    // Activates activeBuffer as the draw target, optionally clearing it.
    void ResetDrawBuffers(FBO* activeBuffer, bool clearBuffer = true);
};

// =================================================================================================
