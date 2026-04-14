#define NOMINMAX

#include "cubemap.h"
#include "texturebuffer.h"
#include "descriptor_heap.h"
#include "command_queue.h"
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
    if (m_buffers.IsEmpty())
        return false;

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    TextureBuffer* first = m_buffers[0];
    int w = first->m_info.m_width;
    int h = first->m_info.m_height;
    if (w <= 0 or h <= 0)
        return false;

    bool wasRecording = commandQueueHandler.Get().m_isRecording;
    if (FAILED(device->GetDeviceRemovedReason())) {
        fprintf(stderr, "Cubemap::Deploy: device already removed at entry (wasRecording=%d)\n", wasRecording);
        fflush(stderr);
        return false;
    }
    fprintf(stderr, "Cubemap::Deploy: enter w=%d h=%d wasRecording=%d\n", w, h, wasRecording);
    fflush(stderr);

    // (Re-)create cubemap resource: 6-slice array
    m_resource.Reset();
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = UINT(w);
    rd.Height = UINT(h);
    rd.DepthOrArraySize = 6;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_resource));
    if (FAILED(hr))
        return false;

    // Upload all 6 faces into a single command list; each face has its own upload buffer.
    // All upload buffers are kept alive in uploads[] until after Flush().
    auto* cq = commandQueueHandler.GetOpenClean();
    if (not cq) {
        fprintf(stderr, "Cubemap::Deploy: GetOpenClean failed (device removed: 0x%08X)\n",
            (unsigned)device->GetDeviceRemovedReason());
        fflush(stderr);
        return false;
    }
    auto* list = cq->List();

    int faceCount = m_buffers.Length();
    ComPtr<ID3D12Resource> uploads[6];
    for (int face = 0; face < 6; ++face) {
        TextureBuffer* tb = m_buffers[face < faceCount ? face : faceCount - 1];
        if (not UploadSubresource(device, list, m_resource.Get(), UINT(face), tb->DataBuffer(), w, h, 4, uploads[face], /*addBarrier=*/false))
            return false;
    }

#if 1
    SubresourceBarrier(list, m_resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
#else
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = m_resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
#endif
    cq->Flush();

    // Create / update SRV
    if (m_handle == UINT32_MAX) {
        DescriptorHandle hdl = descriptorHeaps.AllocSRV();
        if (not hdl.IsValid())
            return false;
        m_handle = hdl.index;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = descriptorHeaps.m_srvHeap.CpuHandle(m_handle);
    device->CreateShaderResourceView(m_resource.Get(), &srvDesc, cpuHandle);

    m_isValid = true;
    m_hasBuffer = true;
    return true;
}

// =================================================================================================
