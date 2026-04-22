#define NOMINMAX

#include <utility>
#include <stdio.h>

#include "std_defines.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL_image.h"
#pragma warning(pop)

#include "texture.h"
#include "gfxstates.h"
#include "base_renderer.h"
#include "descriptor_heap.h"
#include "commandlist.h"
#include "dx12context.h"
#include "dx12upload.h"
#include "resource_handler.h"

// =================================================================================================
// DX12 Texture implementation

uint32_t Texture::nullHandle = UINT32_MAX;

int Texture::CompareTextures(void* context, const String& k1, const String& k2) {
    int i = String::Compare(nullptr, k1, k2);
    return (i < 0) ? -1 : (i > 0) ? 1 : 0;
}

// =================================================================================================

Texture::Texture(uint32_t handle, TextureType type, GfxWrapMode wrap)
    : m_handle(handle)
    , m_type(type)
    , m_tmuIndex(-1)
    , m_wrapMode(wrap)
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
        m_handle = other.m_handle;
        m_resource = other.m_resource;  // shared
        m_name = other.m_name;
        m_buffers = other.m_buffers;
        m_filenames = other.m_filenames;
        m_type = other.m_type;
        m_wrapMode = other.m_wrapMode;
        m_useMipMaps = other.m_useMipMaps;
        m_hasBuffer = other.m_hasBuffer;
        m_hasParams = other.m_hasParams;
        m_isValid = other.m_isValid;
    }
    return *this;
}


Texture& Texture::Move(Texture& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_handle = std::exchange(other.m_handle, UINT32_MAX);
        m_resource = std::move(other.m_resource);
        m_name = std::move(other.m_name);
        m_buffers = std::move(other.m_buffers);
        m_filenames = std::move(other.m_filenames);
        m_type = other.m_type;
        m_wrapMode = other.m_wrapMode;
        m_useMipMaps = other.m_useMipMaps;
        m_hasBuffer = other.m_hasBuffer;
        m_hasParams = other.m_hasParams;
        m_isValid = std::exchange(other.m_isValid, false);
    }
    return *this;
}


bool Texture::Create(void)
{
    Destroy();
    // Allocate an SRV descriptor index.
    DescriptorHandle hdl = descriptorHeaps.AllocSRV();
    if (not hdl.IsValid()) 
        return false;
    m_handle = hdl.index;
    m_isValid = true;
    return true;
}


void Texture::Destroy(void)
{
    if (m_isValid) {
        m_isValid = false;
        m_isDeployed = false;
        if (m_isDisposable)
            gfxResourceHandler.Track(m_resource);
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
    return m_isValid && m_handle != UINT32_MAX
        and (HasBuffer() or (not m_buffers.IsEmpty() and IsDeployed()));
}


bool Texture::Bind(int tmuIndex, bool isDeploying)
{
    if (not isDeploying and not IsAvailable())
        return false;
    m_tmuIndex = tmuIndex;
    gfxStates.BindTexture(TextureTypeToGLenum(m_type), m_handle, tmuIndex);

    // DX12: bind this texture to its slot in the root signature.
    // Root params kSrvBase..kSrvBase+15 are individual 1-entry descriptor tables for t0..t15.
    // SetDescriptorHeaps is called once in Shader::Enable(); here we only
    // update the per-slot root parameter.
    auto* list = commandListHandler.CurrentList();
    if (list and (m_handle != UINT32_MAX)) {
        auto& heap = descriptorHeaps.m_srvHeap;
        if (heap.m_heap)
            list->SetGraphicsRootDescriptorTable(UINT(Shader::kSrvBase + tmuIndex), heap.GpuHandle(m_handle));
    }
    return true;
}


void Texture::Release(void)
{
    if (m_tmuIndex >= 0) {
        gfxStates.BindTexture(TextureTypeToGLenum(m_type), UINT32_MAX, m_tmuIndex);
        m_tmuIndex = -1;
    }
}


void Texture::SetParams(bool /*forceUpdate*/)
{
    // Sampling params are configured via static samplers in the root signature.
    // No per-texture sampler state in DX12 (static samplers cover the standard cases).
    m_hasParams = true;
}


bool Texture::CreateTextureResource(int w, int h, int arraySize)
{
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = UINT(w);
    rd.Height = UINT(h);
    rd.DepthOrArraySize = UINT16(arraySize);
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_resource));
    if (FAILED(hr))
        return false;
#ifdef _DEBUG
    char name[128];
    snprintf(name, sizeof(name), "Texture[%s]", (const char*)m_name);
    m_resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif
    return true;
}


bool Texture::CreateSRV(void)
{
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    if (m_handle == UINT32_MAX) {
        DescriptorHandle hdl = descriptorHeaps.AllocSRV();
        if (not hdl.IsValid())
            return false;
        m_handle = hdl.index;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (m_type == TextureType::CubeMap) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = 1;
    }
    else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = descriptorHeaps.m_srvHeap.CpuHandle(m_handle);
    device->CreateShaderResourceView(m_resource.Get(), &srvDesc, cpuHandle);
    return true;
}


bool Texture::Deploy(int bufferIndex)
{
    if (m_isDeployed)
        return true;
    if (not Bind(0, true))
        return false;
    if (bufferIndex >= m_buffers.Length())
        return false;
    TextureBuffer* tb = m_buffers[bufferIndex];
    if (not tb)
        return false;

    int w = tb->m_info.m_width;
    int h = tb->m_info.m_height;
    if (w <= 0 or h <= 0)
        return false;

    if (not CreateTextureResource(w, h, 1))
        return false;
    const uint8_t* pixels = static_cast<const uint8_t*>(tb->DataBuffer());
    if (not UploadTextureData(dx12Context.Device(), m_resource.Get(), pixels, w, h, tb->m_info.m_componentCount))
        return false;
    if (not CreateSRV())
        return false;

    m_hasBuffer = true;
    m_isDeployed = true;
    return true;
}


bool Texture::Redeploy(void)
{
    m_isDeployed = false;
    return Deploy(0);
}


bool Texture::Load(String& folder, List<String>& fileNames, const TextureCreationParams& params)
{
    m_filenames = fileNames;
    m_name = fileNames.First();
    TextureBuffer* texBuf = nullptr;
    for (auto& fileName : fileNames) {
        if (fileName.IsEmpty()) {
            if (not texBuf)
                return false;
            ++(texBuf->m_refCount);
            m_buffers.Append(texBuf);
        }
        else {
            String fullPath = folder + "/" + fileName;
            SDL_Surface* image = IMG_Load((const char*)fullPath);
            if (not image) {
                if (params.isRequired)
                    fprintf(stderr, "Texture::Load: failed to load '%s'\n", (const char*)fullPath);
                return false;
            }
            texBuf = new TextureBuffer();
            texBuf->Create(image, params.premultiply, params.flipVertically);
            m_buffers.Append(texBuf);
        }
    }
    return true;
}


bool Texture::CreateFromFile(String folder, List<String>& fileNames, const TextureCreationParams& params)
{
    if (not Create())
        return false;
    if (fileNames.IsEmpty())
        return true;
    if (not Load(folder, fileNames, params))
        return false;
    if (params.cartoonize)
        Cartoonize(params.blur, params.gradients, params.outline);
    m_isDisposable = params.isDisposable;
    return Deploy();
}


bool Texture::CreateFromSurface(SDL_Surface* surface, const TextureCreationParams& params)
{
    if (not Create())
        return false;
    m_buffers.Append(new TextureBuffer(surface, params.premultiply, params.flipVertically));
    m_isDisposable = params.isDisposable;
    return Deploy();
}


void Texture::Cartoonize(uint16_t blurStrength, uint16_t gradients, uint16_t outlinePasses)
{
    for (auto& b : m_buffers)
        b->Cartoonize(blurStrength, gradients, outlinePasses);
}


void Texture::SetWrapping(int wrapMode) noexcept
{
    if (wrapMode >= 0) m_wrapMode = GfxWrapMode(wrapMode);
    // Wrapping is configured via static samplers / PSO; no per-texture state in DX12.
}


RenderOffsets Texture::ComputeOffsets(int w, int h, int viewportWidth, int viewportHeight, int renderAreaWidth, int renderAreaHeight)
noexcept
{
    if (renderAreaWidth == 0)
        renderAreaWidth = viewportWidth;
    if (renderAreaHeight == 0)
        renderAreaHeight = viewportHeight;
    float xScale = float(renderAreaWidth) / float(viewportWidth);
    float yScale = float(renderAreaHeight) / float(viewportHeight);
    float wRatio = float(renderAreaWidth) / float(w);
    float hRatio = float(renderAreaHeight) / float(h);
    RenderOffsets offsets = { 0.5f * xScale, 0.5f * yScale };
    if (wRatio > hRatio)
        offsets.x -= (float(renderAreaWidth) - float(w) * hRatio) / float(2 * viewportWidth);
    else if (wRatio < hRatio)
        offsets.y -= (float(renderAreaHeight) - float(h) * wRatio) / float(2 * viewportHeight);
    return offsets;
}

// =================================================================================================

void TiledTexture::SetParams(bool /*forceUpdate*/) { m_hasParams = true; }
void RenderTargetTexture::SetParams(bool /*forceUpdate*/)   { m_hasParams = true; }
void ShadowTexture::SetParams(bool /*forceUpdate*/) { m_hasParams = true; }

// =================================================================================================
