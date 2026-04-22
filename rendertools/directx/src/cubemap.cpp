#define NOMINMAX

#include "cubemap.h"
#include "texturebuffer.h"
#include "descriptor_heap.h"
#include "dx12context.h"
#include "dx12upload.h"

// =================================================================================================
// DX12 Cubemap implementation

void Cubemap::SetParams(void) {
    // Sampling parameters are baked into the root-signature static samplers.
    m_hasParams = true;
}


bool Cubemap::Deploy(int /*bufferIndex*/)
{
    if (m_isDeployed)
        return true;
    if (not Bind(0, true))
        return false;
    if (m_buffers.IsEmpty())
        return false;

    if (not CreateTextureResource(w, h, 6))
        return false;

    int faceCount = m_buffers.Length();
    const uint8_t* faces[6];
    for (int i = 0; i < 6; ++i)
        faces[i] = m_buffers[i < faceCount ? i : faceCount - 1]->DataBuffer();
    if (not UploadTextureData(dx12Context.Device(), m_resource.Get(), faces, 6, w, h, 4))
        return false;

    if (not CreateSRV())
        return false;

    m_hasBuffer = true;
    m_isDeployed = true;
    return true;
}

// =================================================================================================
