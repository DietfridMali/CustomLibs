#pragma once

#include <math.h>
#include <utility>
#include <stdlib.h>

#include "std_defines.h"
#include "basesingleton.hpp"
#include "array.hpp"

// =================================================================================================

class RenderTarget;

class DrawBufferHandler
{
public:
    using DrawBufferList = AutoArray <GfxTypes::Uint>;

protected:
    RenderTarget*       m_activeBuffer{ nullptr };
    List<RenderTarget*> m_drawBufferStack{};
    int                 m_windowWidth{ 0 };
    int                 m_windowHeight{ 0 };

public:
    DrawBufferHandler() = default;

    ~DrawBufferHandler() = default;

    void Setup(int windowWidth, int windowHeight);

    void ActivateDrawBuffer(RenderTarget* buffer);

    bool DeactivateDrawBuffer(RenderTarget* buffer);

    void ResetDrawBuffers(void);

    void SetActiveDrawBuffers(void);

    inline RenderTarget* GetActiveBuffer(void) noexcept {
        return m_activeBuffer;
    }
};

// =================================================================================================
