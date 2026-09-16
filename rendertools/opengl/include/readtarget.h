#pragma once

#include "glew.h"
#include "base_readtarget.h"

class GfxReadTarget
    : public BaseReadTarget
{
private:
    GLuint      m_handle{ 0 };
    GLsync      m_fence{ nullptr };
    uint8_t*    m_mapped{ nullptr };

    void DeleteFence(void) noexcept;

public:
    GfxReadTarget(String name = "")
        : BaseReadTarget(name)
    {}

    virtual ~GfxReadTarget() {
        Destroy();
    }

    virtual bool Request(RenderTarget& renderTarget, int bufferIndex, int arraySlice = 0) override;

    virtual State Poll(void) override;

    virtual const uint8_t* Data(void) override;

    virtual void Release(void) override;

    virtual void Destroy(void) override;

    bool Allocate(size_t size);

    bool Submit(size_t size, int width, int height);

    inline GLuint Handle(void) const noexcept {
        return m_handle;
    }
};
