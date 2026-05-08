#pragma once

#include "vkframework.h"
#include "gfx_buffer.h"
#include "array.hpp"

#include <cstdint>

// =================================================================================================
// GfxDataChunkHandler / GfxDataChunkList — Vulkan port of the DX12 chunk pool.
//
// Per-frame pool of host-visible GfxBuffer chunks. Within a frame, callers (mesh upload paths,
// staging streams) request a chunk of at least N bytes via Update; the list hands out the next
// unused chunk and grows / reallocates as needed. Reset(frameIndex) at frame start rewinds the
// usage counter so the pool can be reused in subsequent frames.
//
// Owned chunks are GfxBuffer* (not GfxBuffer values) because GfxBuffer is non-copyable —
// AutoArray<GfxBuffer*> keeps lifetime explicit.

class GfxDataChunkList
{
public:
    AutoArray<GfxBuffer*> m_chunks;
    int32_t               m_usedChunks { 0 };

    void Reset(void) {
        m_usedChunks = 0;
    }

    void Clear(void) {
        for (auto& c : m_chunks) {
            if (c) {
                c->Destroy();
                delete c;
            }
        }
        m_chunks.Clear();
        m_usedChunks = 0;
    }

    // Returns a chunk of at least dataSize bytes. Grows or reallocates the slot as needed.
    // Returns nullptr on allocation failure.
    GfxBuffer* Update(size_t dataSize, const char* ownerName, const char* type, uint64_t execId);

    inline GfxBuffer* GetResource(void) {
        return (m_usedChunks > 0) ? m_chunks[m_usedChunks - 1] : nullptr;
    }
};


class GfxDataChunkHandler
{
private:
    AutoArray<GfxDataChunkList> m_chunkLists;
    uint32_t                    m_frameCount;

public:
    GfxDataChunkHandler(uint32_t frameCount = 2)
        : m_frameCount(frameCount)
    {
        m_chunkLists.Resize(m_frameCount);
    }

    ~GfxDataChunkHandler() {
        Clear();
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

    GfxBuffer* Update(uint32_t fi, size_t dataSize, const char* ownerName, const char* type, uint64_t execId) {
        return (fi >= m_frameCount) ? nullptr : m_chunkLists[fi].Update(dataSize, ownerName, type, execId);
    }
};

// =================================================================================================
