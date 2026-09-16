#pragma once

#include "dx12framework.h"
#include "array.hpp"
#include "base_readtarget.h"

class GfxReadTarget
    : public BaseReadTarget
{
private:
    ComPtr<ID3D12Resource>              m_resource;
    uint8_t*                            m_mapped{ nullptr };
    AutoArray<uint8_t>                  m_pixels;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT  m_layout{};
    UINT                                m_rowCount{ 0 };
    UINT64                              m_rowSize{ 0 };

    bool IsComplete(void) noexcept;

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

    bool Allocate(UINT64 totalSize);

    bool Submit(const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout, UINT rowCount, UINT64 rowSize, int width, int height, uint64_t frame, int slot);

    inline ID3D12Resource* Resource(void) const noexcept {
        return m_resource.Get();
    }
};
