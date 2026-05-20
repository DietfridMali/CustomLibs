#include "meshhandler.h"

// =================================================================================================
// MeshHandler implementation. See meshhandler.h for the pooling / recycling model.

void MeshHandler::Recycle(void) noexcept
{
    // Reclaim the slot that is about to be reused this frame. CommandQueue::BeginFrame has already
    // waited this slot's frame fence, so its parked meshes are GPU-idle.
    MeshTable& usedList = m_usedLists[commandListHandler.CmdQueue().FrameIndex()];
    for (Mesh* mesh : usedList)
        m_freeList.Push(mesh);
    usedList.Clear();
    m_frameNumber = commandListHandler.CmdQueue().FrameNumber();
}


Mesh* MeshHandler::AllocMesh(uint32_t meshBufferMask) noexcept
{
    if (m_frameNumber != commandListHandler.CmdQueue().FrameNumber())
        Recycle();

    Mesh* mesh = nullptr;
    for (int32_t i = 0; i < m_freeList.Length(); ++i) {
        Mesh* candidate = m_freeList[i];
        if ((meshBufferMask == 0) or (candidate->m_meshBufferMask == meshBufferMask)) {
            m_freeList[i] = m_freeList[m_freeList.Length() - 1];
            m_freeList.Pop();
            mesh = candidate;
            break;
        }
    }
    if (not mesh)
        mesh = new Mesh();
    mesh->ResetGfxData();
    m_usedLists[commandListHandler.CmdQueue().FrameIndex()].Push(mesh);
    return mesh;
}


void MeshHandler::Destroy(void) noexcept
{
    for (Mesh* mesh : m_freeList)
        delete mesh;
    m_freeList.Clear();
    for (MeshTable& usedList : m_usedLists) {
        for (Mesh* mesh : usedList)
            delete mesh;
        usedList.Clear();
    }
}

// =================================================================================================
