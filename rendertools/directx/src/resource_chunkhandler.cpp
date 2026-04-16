
#include "resource_chunkhandler.h"
#include <cstdio>
#include <cstring>

// =================================================================================================

void GfxDataChunkList::PrepareResourceDesc(D3D12_RESOURCE_DESC& rd, size_t dataSize) {
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = dataSize;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
}


ComPtr<ID3D12Resource> GfxDataChunkList::Update(size_t dataSize, const char* ownerName, uint64_t execId) {
    uint32_t updateResource = 0;

    if (m_usedChunks >= m_chunks.Length()) {
        // grow pool: allocate new chunk
        updateResource = 1;
        m_chunks.Append();
    }
    else if (m_chunks[m_usedChunks]->GetDesc().Width < dataSize) {
        updateResource = 2;
        // existing chunk too small — reallocate in place
        m_chunks[m_usedChunks].Reset();
    }

    if (updateResource) {
        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC rd{};
        PrepareResourceDesc(rd, dataSize);
        HRESULT hr = dx12Context.Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_chunks[m_usedChunks]));
        if (FAILED(hr)) {
            if (updateResource == 1)
                m_chunks.Pop();
            return nullptr;
        }
    }
    return m_chunks[m_usedChunks++];
}

// =================================================================================================
