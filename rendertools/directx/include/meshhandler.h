#pragma once

#include <cstdint>

#include "array.hpp"
#include "basesingleton.hpp"
#include "mesh.h"
#include "commandlist.h"

// =================================================================================================
// MeshHandler: pool of reusable Mesh objects for meshes that are rebuilt every frame (text meshes
// first; wider use is evaluated later).
//
// AllocMesh hands out a recycled or freshly created Mesh and parks it in the used list of the
// current frame slot. When that slot is entered again FRAME_COUNT frames later, its GPU work has
// passed the frame fence, so the parked meshes are safe to move back to the free list. Recycling
// is lazy: the first AllocMesh of a new frame (detected via CommandListHandler::FrameNumber)
// reclaims the slot that is about to be reused — no per-frame hook needed.

class MeshHandler
    : public BaseSingleton<MeshHandler>
{
public:
    using MeshTable = AutoArray<Mesh*>;

private:
    MeshTable                                          m_freeList;
    StaticArray<MeshTable, CommandQueue::FRAME_COUNT>   m_usedLists;
    uint64_t                                           m_frameNumber{ 0 };

    void Recycle(void) noexcept;

public:
    // Returns a Mesh with its CPU buffers reset. With a non-zero meshBufferMask a free mesh of
    // exactly that buffer composition (eMeshBufferBits) is reused when available; 0 matches any.
    // The caller builds the buffers and must not delete the mesh — it returns to the pool by
    // itself once the frame fence has passed.
    Mesh* AllocMesh(uint32_t meshBufferMask = 0) noexcept;

    // Deletes every pooled mesh (free list + all used lists). Call during controlled shutdown,
    // after the GPU is idle.
    void Destroy(void) noexcept;
};

#define meshHandler MeshHandler::Instance()

// =================================================================================================
