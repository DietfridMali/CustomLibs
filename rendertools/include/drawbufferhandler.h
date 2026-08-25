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

    // Custom draw-buffer setup (RenderTarget::SelectCustomDrawBuffers). Entry i is the BUFFER INDEX of the
    // render target's buffer bound to fragment output slot i, or CUSTOM_DRAW_BUFFER_NONE for a slot that is
    // left unwritten. API-neutral on purpose: the OpenGL backend translates the indices into attachment
    // points, DX and Vulkan into RTVs / colour attachments, so one and the same list works everywhere.
    using CustomDrawBufferList = AutoArray <int>;

    static constexpr int CUSTOM_DRAW_BUFFER_NONE = -1;

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

    inline bool IsActiveDrawBuffer(RenderTarget* buffer) noexcept {
        return (buffer != nullptr) and (buffer == m_activeBuffer);
    }
};

// =================================================================================================
