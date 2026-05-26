#pragma once

#include "dx12framework.h"
#include "rendertypes.h"

class Texture;

// =================================================================================================
// Low-level: records CopyTextureRegion for one subresource into an already-open list.
// outUpload receives the staging buffer; caller must keep it alive until the list is flushed.

void SubresourceBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, UINT subresource);

bool UploadSubresource(ID3D12Device* device,
                       ID3D12GraphicsCommandList* list,
                       ID3D12Resource* dstResource,
                       UINT subresource,
                       const uint8_t* pixels,
                       int width, int height, int channels,
                       ComPtr<ID3D12Resource>& outUpload,
                       bool addBarrier = true) noexcept;

// =================================================================================================
// High-level: open upload list, copy all subresources, flush — no list management by the caller.

// Upload faceCount subresources (1 for a plain 2D texture, 6 for a cubemap).
bool UploadTextureData(ID3D12Device* device, ID3D12Resource* dstResource, const uint8_t* const* faces, int faceCount, int width, int height, int channels) noexcept;

// Single-subresource convenience.
inline bool UploadTextureData(ID3D12Device* device, ID3D12Resource* dstResource, const uint8_t* pixels, int width, int height, int channels) noexcept {
    return UploadTextureData(device, dstResource, &pixels, 1, width, height, channels);
}

// Create + upload a Texture3D resource. Returns nullptr on failure.
ComPtr<ID3D12Resource> Upload3DTextureData(ID3D12Device* device, int w, int h, int d, DXGI_FORMAT fmt, uint32_t pixelStride, const void* data) noexcept;

// =================================================================================================
// Platform-neutral upload entry points used by base_noisetexture. Each function does the full
// per-Deploy pipeline for an existing Texture: releases any previously-owned ID3D12Resource,
// creates a new committed resource of the format resolved via ToDXGIFormat(GfxPixelFormat),
// uploads the pixel data, (re)allocates an SRV handle in the global descriptor heap, calls
// tex.SetParams(false), and sets tex.m_isValid + tex.m_isDeployed on success.

bool Upload2DTexture(Texture& tex, int width, int height,
                     GfxPixelFormat fmt, const void* data) noexcept;

bool Upload3DTexture(Texture& tex, int width, int height, int depth,
                     GfxPixelFormat fmt, const void* data) noexcept;

// =================================================================================================
