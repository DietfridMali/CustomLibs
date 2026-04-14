#pragma once

#include "framework.h"

// =================================================================================================
// DX12 upload helper — records a CopyTextureRegion for one subresource via a staging buffer.
// outUpload receives the upload heap buffer; the caller must keep it alive until Flush().

void SubresourceBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, UINT subResource);

bool UploadSubresource(ID3D12Device* device,
                       ID3D12GraphicsCommandList* list,
                       ID3D12Resource* dstResource,
                       UINT subresource,
                       const uint8_t* pixels,
                       int width, int height, int channels,
                       ComPtr<ID3D12Resource>& outUpload,
                       bool addBarrier = true) noexcept;

// =================================================================================================
