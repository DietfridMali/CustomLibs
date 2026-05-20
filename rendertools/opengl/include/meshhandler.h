#pragma once

#include <cstdint>

#include "basesingleton.hpp"
#include "mesh.h"

// =================================================================================================
// MeshHandler (OpenGL): minimal stub. OpenGL does not need the per-frame mesh pool — the GL driver
// renames / orphans dynamic buffers internally, so a single reused mesh is safe both across and
// within frames. AllocMesh hands back one shared, reset mesh and flags its buffers (meshBufferMask)
// dynamic — GfxDataBuffer::Update skips the upload for an already-allocated *static* buffer, so
// without that flag every text after the first keeps the first one's data.

class MeshHandler
    : public BaseSingleton<MeshHandler>
{
private:
    Mesh m_mesh;

public:
    inline Mesh* AllocMesh(uint32_t meshBufferMask = 0) noexcept {
        m_mesh.SetDynamic(meshBufferMask);
        m_mesh.ResetGfxData();
        return &m_mesh;
    }

    inline void Destroy(void) noexcept { }
};

#define meshHandler MeshHandler::Instance()

// =================================================================================================
