#define NOMINMAX

#include <utility>
#include <stdio.h>
#include <stdexcept>
#include <cstring>

#include "std_defines.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL_image.h"
#pragma warning(pop)

#include "texture.h"
#include "opengl_states.h"
#include "base_renderer.h"
#include "descriptor_heap.h"
#include "command_queue.h"
#include "dx12context.h"

// =================================================================================================
// DX12 Texture implementation

uint32_t Texture::nullHandle = UINT32_MAX;

int Texture::CompareTextures(void* context, const String& k1, const String& k2) {
    int i = String::Compare(nullptr, k1, k2);
    return (i < 0) ? -1 : (i > 0) ? 1 : 0;
}

// -------------------------------------------------------------------------------------------------
// Helpers

// Upload pixel data to a default-heap texture resource via a temporary upload buffer.
static bool UploadTextureData(
    ID3D12Device*    device,
    ID3D12Resource*  dstResource,
    const uint8_t*   pixelData,
    int              width, int height, int channels) noexcept
{
    DXGI_FORMAT fmt = (channels == 4) ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rowCount = 0;
    UINT64 rowSize = 0;
    D3D12_RESOURCE_DESC desc = dstResource->GetDesc();
    device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rowCount, &rowSize, &uploadSize);

    // Upload heap resource
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC upDesc{};
    upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upDesc.Width     = uploadSize;
    upDesc.Height    = upDesc.DepthOrArraySize = upDesc.MipLevels = 1;
    upDesc.SampleDesc.Count = 1;
    upDesc.Layout    = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> upload;
    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &upDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
    if (FAILED(hr)) return false;

    // Copy to upload heap with row padding
    uint8_t* mapped = nullptr;
    D3D12_RANGE mapRange{ 0, 0 };
    if (FAILED(upload->Map(0, &mapRange, (void**)&mapped))) return false;
    for (UINT r = 0; r < rowCount; ++r) {
        const uint8_t* src = pixelData + r * UINT(width) * channels;
        uint8_t* dst = mapped + layout.Offset + r * layout.Footprint.RowPitch;

        if (channels == 4) {
            std::memcpy(dst, src, UINT(width) * 4);
        } else if (channels == 3) {
            // Expand RGB → RGBA
            for (int x = 0; x < width; ++x) {
                dst[x * 4 + 0] = src[x * 3 + 0];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        } else {
            std::memcpy(dst, src, rowSize);
        }
    }
    upload->Unmap(0, nullptr);

    // Issue copy command
    auto* list = cmdQueue.List();
    if (!list) return false;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = dstResource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{}, dstLoc{};
    srcLoc.pResource       = upload.Get();
    srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = layout;
    dstLoc.pResource       = dstResource;
    dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    list->ResourceBarrier(1, &barrier);

    // Keep upload buffer alive until the GPU finishes (simplistic: flush immediately)
    cmdQueue.Execute();
    cmdQueue.WaitIdle();

    return true;
}

// =================================================================================================

Texture::Texture(uint32_t handle, int type, int wrapMode)
    : m_handle(handle)
    , m_type(type)
    , m_tmuIndex(-1)
    , m_wrapMode(wrapMode)
    , m_name("")
{
    SetupLUT();
}


Texture::~Texture() noexcept
{
    if (m_isValid) {
        if (UpdateLUT()) {
            textureLUT.Remove(m_name);
            m_name = "";
        }
        Destroy();
    }
}


Texture& Texture::Copy(const Texture& other)
{
    if (this != &other) {
        Destroy();
        m_handle     = other.m_handle;
        m_resource   = other.m_resource;  // shared
        m_name       = other.m_name;
        m_buffers    = other.m_buffers;
        m_filenames  = other.m_filenames;
        m_type       = other.m_type;
        m_wrapMode   = other.m_wrapMode;
        m_useMipMaps = other.m_useMipMaps;
        m_hasBuffer  = other.m_hasBuffer;
        m_hasParams  = other.m_hasParams;
        m_isValid    = other.m_isValid;
    }
    return *this;
}


Texture& Texture::Move(Texture& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_handle     = std::exchange(other.m_handle, UINT32_MAX);
        m_resource   = std::move(other.m_resource);
        m_name       = std::move(other.m_name);
        m_buffers    = std::move(other.m_buffers);
        m_filenames  = std::move(other.m_filenames);
        m_type       = other.m_type;
        m_wrapMode   = other.m_wrapMode;
        m_useMipMaps = other.m_useMipMaps;
        m_hasBuffer  = other.m_hasBuffer;
        m_hasParams  = other.m_hasParams;
        m_isValid    = std::exchange(other.m_isValid, false);
    }
    return *this;
}


bool Texture::Create(void)
{
    Destroy();
    // Allocate an SRV descriptor index.
    DescriptorHandle hdl = descriptorHeaps.AllocSRV();
    if (!hdl.IsValid()) return false;
    m_handle  = hdl.index;
    m_isValid = true;
    return true;
}


void Texture::Destroy(void)
{
    if (m_isValid) {
        m_isValid = false;
        // Note: descriptor allocator doesn't support free in this implementation.
        // The handle index is not returned to the heap (acceptable for now).
        m_handle = UINT32_MAX;
        m_resource.Reset();
        for (auto* p : m_buffers) {
            if (p->m_refCount) --p->m_refCount;
            else delete p;
        }
        m_buffers.Clear();
        m_hasBuffer = false;
    }
}


bool Texture::IsAvailable(void)
{
    return m_isValid && m_handle != UINT32_MAX;
}


bool Texture::Bind(int tmuIndex)
{
    if (!IsAvailable()) return false;
    m_tmuIndex = tmuIndex;
    openGLStates.BindTexture(m_type, m_handle, tmuIndex);
    return true;
}


void Texture::Release(void)
{
    if (m_tmuIndex >= 0) {
        openGLStates.BindTexture(m_type, UINT32_MAX, m_tmuIndex);
        m_tmuIndex = -1;
    }
}


void Texture::SetParams(bool /*forceUpdate*/)
{
    // Sampling params are configured via static samplers in the root signature.
    // No per-texture sampler state in DX12 (static samplers cover the standard cases).
    m_hasParams = true;
}


bool Texture::Deploy(int bufferIndex)
{
    if (bufferIndex >= m_buffers.Length()) return false;
    TextureBuffer* tb = m_buffers[bufferIndex];
    if (!tb) return false;

    ID3D12Device* device = dx12Context.Device();
    if (!device) return false;

    int w        = tb->m_info.m_width;
    int h        = tb->m_info.m_height;
    int channels = tb->m_info.m_channels;
    if (w <= 0 || h <= 0) return false;

    // (Re-)create the default-heap texture resource
    m_resource.Reset();
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = UINT(w);
    rd.Height           = UINT(h);
    rd.DepthOrArraySize = (m_type == GL_TEXTURE_CUBE_MAP) ? 6 : 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_resource));
    if (FAILED(hr)) return false;

    // Upload pixel data
    const uint8_t* pixels = static_cast<const uint8_t*>(tb->DataBuffer());
    if (!UploadTextureData(device, m_resource.Get(), pixels, w, h, channels))
        return false;

    // Create / update SRV
    if (m_handle == UINT32_MAX) {
        DescriptorHandle hdl = descriptorHeaps.AllocSRV();
        if (!hdl.IsValid()) return false;
        m_handle = hdl.index;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = (m_type == GL_TEXTURE_CUBE_MAP)
                                    ? D3D12_SRV_DIMENSION_TEXTURECUBE
                                    : D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = descriptorHeaps.m_srvHeap.CpuHandle(m_handle);
    device->CreateShaderResourceView(m_resource.Get(), &srvDesc, cpuHandle);

    m_isValid   = true;
    m_hasBuffer = true;
    return true;
}


bool Texture::Redeploy(void)
{
    return Deploy(0);
}


bool Texture::Load(String& folder, List<String>& fileNames,
                   const TextureCreationParams& params)
{
    return CreateFromFile(folder, fileNames, params);
}


bool Texture::CreateFromFile(String folder, List<String>& fileNames,
                              const TextureCreationParams& params)
{
    for (auto& fname : fileNames) {
        String fullPath = folder + "/" + fname;
        SDL_Surface* surface = IMG_Load((const char*)fullPath);
        if (!surface) {
            if (params.isRequired)
                fprintf(stderr, "Texture::CreateFromFile: failed to load '%s'\n", (const char*)fullPath);
            continue;
        }
        if (!CreateFromSurface(surface, params)) {
            SDL_FreeSurface(surface);
            return false;
        }
        SDL_FreeSurface(surface);
        Register(fname);
        return true;
    }
    return false;
}


bool Texture::CreateFromSurface(SDL_Surface* surface, const TextureCreationParams& params)
{
    if (!surface) return false;

    // Convert to RGBA if needed
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (!converted) return false;

    int w = converted->w;
    int h = converted->h;

    // Store pixel data in a TextureBuffer
    TextureBuffer* tb = new (std::nothrow) TextureBuffer();
    if (!tb) { SDL_FreeSurface(converted); return false; }

    SDL_LockSurface(converted);

    tb->m_info.m_width    = w;
    tb->m_info.m_height   = h;
    tb->m_info.m_channels = 4;
    tb->Resize(w * h * 4);
    std::memcpy(tb->DataBuffer(), converted->pixels, w * h * 4);

    SDL_UnlockSurface(converted);
    SDL_FreeSurface(converted);

    if (!m_buffers.IsEmpty())
        m_buffers.Clear();
    m_buffers.Append(tb);
    m_hasBuffer = true;

    if (!Create()) return false;
    if (!Deploy(0)) return false;

    if (params.cartoonize)
        Cartoonize(params.blur, params.gradients, params.outline);

    m_isValid = true;
    return true;
}


void Texture::Cartoonize(uint16_t /*blurStrength*/, uint16_t /*gradients*/, uint16_t /*outlinePasses*/)
{
    // Post-process: not implemented for DX12 port yet.
}


void Texture::SetWrapping(int wrapMode) noexcept
{
    if (wrapMode >= 0) m_wrapMode = wrapMode;
    // Wrapping is configured via static samplers / PSO; no per-texture state in DX12.
}


RenderOffsets Texture::ComputeOffsets(int w, int h,
                                       int viewportWidth, int viewportHeight,
                                       int renderAreaWidth, int renderAreaHeight) noexcept
{
    float scaleX = (renderAreaWidth  > 0) ? float(w) / float(renderAreaWidth)  : 1.0f;
    float scaleY = (renderAreaHeight > 0) ? float(h) / float(renderAreaHeight) : 1.0f;
    return { (viewportWidth  - renderAreaWidth)  * 0.5f * scaleX,
             (viewportHeight - renderAreaHeight) * 0.5f * scaleY };
}

// =================================================================================================

void TiledTexture::SetParams(bool /*forceUpdate*/) { m_hasParams = true; }
void FBOTexture::SetParams(bool /*forceUpdate*/)   { m_hasParams = true; }
void ShadowTexture::SetParams(bool /*forceUpdate*/) { m_hasParams = true; }

// =================================================================================================
