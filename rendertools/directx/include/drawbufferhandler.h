#pragma once

#include <math.h>
#include <utility>
#include <stdlib.h>

#include "framework.h"
#include "std_defines.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "rendertarget.h"

// =================================================================================================
// DX12 DrawBufferHandler
//
// In OpenGL, multiple color attachments were listed in a GLuint DrawBufferList and passed to
// glDrawBuffers(). In DX12, OMSetRenderTargets accepts an array of D3D12_CPU_DESCRIPTOR_HANDLEs
// (RTVs) and optionally a DSV handle. The RenderTarget class (see rendertarget.h) owns these handles.
//
// This class manages the active render target (RenderTarget), the stack for save/restore, and delegates
// the actual OMSetRenderTargets calls to the active RenderTarget.

class DrawBufferInfo {
public:
    RenderTarget* m_renderTarget{ nullptr };

    DrawBufferInfo(RenderTarget* renderTarget = nullptr) : m_renderTarget(renderTarget) {}

    bool operator==(const DrawBufferInfo& other) const noexcept {
        return other.m_renderTarget == m_renderTarget;
    }
};

// =================================================================================================

class DrawBufferHandler
{
protected:
    RenderTarget*                    m_activeBuffer{ nullptr };
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
    bool SetActiveBuffer(RenderTarget* buffer, bool clearBuffer = false);

    // Establishes the default draw buffer set (called once on renderer creation).
    void SetupDrawBuffers(void);

    // Calls OMSetRenderTargets for the currently tracked draw buffer set.
    void SetActiveDrawBuffers(void);

    // Pushes the current draw buffer state onto the save stack.
    void SaveDrawBuffer(void);

    // Sets renderTarget as the active draw buffer (without the save/restore stack).
    void SetDrawBuffers(RenderTarget* renderTarget);

    // Pops the top draw buffer state from the stack and restores it.
    void RestoreDrawBuffer(void);

    // Removes any reference to buffer from the stack and active state.
    void RemoveDrawBuffer(RenderTarget* buffer);

    // Activates activeBuffer as the draw target, optionally clearing it.
    void ResetDrawBuffers(RenderTarget* activeBuffer, bool clearBuffer = true);
};

// =================================================================================================
