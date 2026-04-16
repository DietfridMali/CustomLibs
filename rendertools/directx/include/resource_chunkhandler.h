#pragma once

#include "dx12context.h"
#include "array.hpp"

// =================================================================================================

struct GfxDataChunkList {
public:
    AutoArray<ComPtr<ID3D12Resource>> m_chunks;
    int32_t                           m_usedChunks{0};

    void Reset(void) {
        m_usedChunks = 0;
    }

    void Clear(void) {
        m_chunks.Clear();
        m_usedChunks = 0;
    }

    ComPtr<ID3D12Resource> Update(size_t dataSize);

    inline ComPtr<ID3D12Resource> GetResource(void) {
        return (m_usedChunks > 0) ? m_chunks[m_usedChunks - 1] : nullptr;
    }

private:
    void PrepareResourceDesc(D3D12_RESOURCE_DESC& rd, size_t dataSize);
};


class GfxDataChunkHandler {
private:
    AutoArray<GfxDataChunkList> m_chunkLists;
    uint32_t                    m_frameCount;

public:
    GfxDataChunkHandler(uint32_t frameCount = 2)
        : m_frameCount(frameCount)
    {
        m_chunkLists.Resize(m_frameCount);
    }

    GfxDataChunkList* GetList(uint32_t frameIndex) {
        return (frameIndex >= m_frameCount) ? nullptr : &m_chunkLists[frameIndex];
    }

    void Reset(uint32_t fi) {
        if (fi < m_frameCount)
            m_chunkLists[fi].Reset();
    }

    void Clear(void) {
        for (uint32_t i = 0; i < m_frameCount; ++i)
            m_chunkLists[i].Clear();
    }

    ComPtr<ID3D12Resource> Update(uint32_t fi, size_t dataSize) {
        return (fi >= m_frameCount) ? nullptr : m_chunkLists[fi].Update(dataSize);
    }
};

// =================================================================================================
