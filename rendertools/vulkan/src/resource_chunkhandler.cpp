#include "resource_chunkhandler.h"

#include <cstdio>

// =================================================================================================
// GfxDataChunkList — Vulkan implementation

GfxBuffer* GfxDataChunkList::Update(size_t dataSize, const char* ownerName, const char* type, uint64_t execId)
{
    if (dataSize == 0)
        return nullptr;

    bool needAlloc = false;

    if (m_usedChunks >= m_chunks.Length()) {
        // Grow pool: append new empty slot.
        GfxBuffer* fresh = new (std::nothrow) GfxBuffer();
        if (not fresh)
            return nullptr;
        m_chunks.Append(fresh);
        needAlloc = true;
    }
    else if (m_chunks[m_usedChunks]->Size() < VkDeviceSize(dataSize)) {
        // Existing chunk too small — destroy and reallocate in place.
#ifdef _DEBUG
        fprintf(stderr, "GfxDataChunkList::Update: chunk %d for command '%s' (execId=%llu) too small (%llu bytes), reallocating to %zu\n",
                m_usedChunks, ownerName ? ownerName : "?", (unsigned long long)execId,
                (unsigned long long)m_chunks[m_usedChunks]->Size(), dataSize);
#endif
        m_chunks[m_usedChunks]->Destroy();
        needAlloc = true;
    }

    GfxBuffer* chunk = m_chunks[m_usedChunks];
    if (needAlloc) {
        // Buffer usage matches the DX12 upload-heap behaviour: writable from CPU + readable
        // by GPU. We tag it as a uniform / vertex / index source so any of the data-buffer
        // call sites can use the same pool. TRANSFER_SRC enables staging-style copies.
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                       | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                       | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                       | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (not chunk->Create(VkDeviceSize(dataSize), usage,
                              VMA_MEMORY_USAGE_AUTO,
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                            | VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
            fprintf(stderr, "GfxDataChunkList::Update: GfxBuffer::Create failed (size=%zu)\n", dataSize);
            return nullptr;
        }
        (void)ownerName; (void)type; (void)execId;  // names are debug-only on Vulkan path
    }
    return m_chunks[m_usedChunks++];
}

// =================================================================================================
