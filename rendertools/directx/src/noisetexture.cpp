#define NOMINMAX

// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cstring>

#include "noisetexture.h"
#include "conversions.hpp"
#include "dx12upload.h"

#pragma warning(push)
#pragma warning(disable:26819)
#include "SDL_image.h"
#pragma warning(pop)

#include "noise.h"

using namespace Noise;

#define NORMALIZE_NOISE 0

#define CLOUD_STRUCTURE 2

// Helper: allocate a SRV for a 3D texture resource
static bool CreateSRV3D(uint32_t& handleOut, ID3D12Resource* resource, DXGI_FORMAT fmt) {
    if (handleOut == UINT32_MAX) {
        DescriptorHandle hdl = descriptorHeaps.AllocSRV();
        if (not hdl.IsValid())
            return false;
        handleOut = hdl.index;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture3D.MipLevels = 1;
    dx12Context.Device()->CreateShaderResourceView(
        resource, &srvDesc, descriptorHeaps.m_srvHeap.CpuHandle(handleOut));
    return true;
}

// =================================================================================================

bool NoiseTexture3D::Allocate(Vector3i gridDimensions) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_gridDimensions = gridDimensions;
    m_data.Resize(GridSize() * 4);
    // Store dimensions; format tags unused in DX12 (0 = unused)
    texBuf->m_info = TextureBuffer::BufferInfo(gridDimensions.x, gridDimensions.y * gridDimensions.z, 1, 0, 0);
    return true;
}


bool NoiseTexture3D::Create(Vector3i gridDimensions, const NoiseParams& params, String noiseFilename, bool deploy) {
    if (not Texture::Create())
        return false;
    m_type = TextureType::Texture3D;
    if (not Allocate(gridDimensions))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        ComputeNoise();
        SaveToFile(noiseFilename);
    }

    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ 0.0f, 0.0f, 0.0f, 0.0f };
    float* data = m_data.Data();
    StaticArray<uint32_t, 101> d[4];
    for (int i = 0; i < 4; ++i)
        d[i].fill(0);
    for (uint32_t i = m_gridDimensions.x * m_gridDimensions.y * m_gridDimensions.z; i; --i) {
        Vector4f noise;
        noise.x = *data++;
        noise.y = *data++;
        noise.z = *data++;
        noise.w = *data++;
        minVals.Minimize(noise);
        maxVals.Maximize(noise);
    }
    if (deploy)
        Deploy();
    return true;
}


static inline float Saturate(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}


static float Modulate(float shape, float coarseDetail, float mediumDetail, float fineDetail) {
    return Saturate(.6125f * shape + .1625f * coarseDetail + .125f * mediumDetail + .1f * fineDetail);
}


void NoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.Data();

    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ -1e6f, -1e6f, -1e6f, -1e6f };

    CloudNoise generator;

    int i = 0;
    Vector3f p;
    for (int z = 0; z < m_gridDimensions.z; ++z) {
        p.z = (float(z) + 0.5f) / float(m_gridDimensions.z);
        for (int y = 0; y < m_gridDimensions.y; ++y) {
            p.y = (float(y) + 0.5f) / float(m_gridDimensions.y);
            for (int x = 0; x < m_gridDimensions.x; ++x) {
                p.x = (float(x) + 0.5f) / float(m_gridDimensions.x);
                Vector4f noise = generator.Compute(p);
                if (m_params.normalize) {
                    data[i++] = noise.x;
                    data[i++] = noise.y;
                    data[i++] = noise.z;
                    data[i++] = noise.a;
                    minVals.Minimize(noise);
                    maxVals.Maximize(noise);
                }
            }
        }
    }

    data = m_data.Data();
    int dataSize = i / 4;
    for (int i = dataSize; i; --i) {
        for (int j = 0; j < 4; ++j, ++data) {
            *data = (m_params.normalize & (1 << j)) ? Conversions::Normalize(*data, minVals[j], maxVals[j]) : Saturate(*data);
        }
    }
}


void NoiseTexture3D::SetParams(bool /*enforce*/) {
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Linear;
    m_sampling.magFilter = GfxFilterMode::Linear;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


bool NoiseTexture3D::Deploy(int) {
    if (m_data.IsEmpty())
        return false;

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    // RGBA16F — 4 channels × float16 per voxel
    // We store as float32; upload as RGBA32F.
    constexpr DXGI_FORMAT fmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr uint32_t    stride = 16;  // 4 × float32

    m_resource = Upload3DTextureData(device, m_gridDimensions.x, m_gridDimensions.y, m_gridDimensions.z, fmt, stride, m_data.Data());
    if (not m_resource)
        return false;
#ifdef _DEBUG
    char name[128];
    snprintf(name, sizeof(name), "NoiseTexture3D[%s]", (const char*)m_name);
    m_resource->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)strlen(name), name);
#endif
    if (not CreateSRV3D(m_handle, m_resource.Get(), fmt))
        return false;

    m_isDeployed = true;
    return true;
}


bool NoiseTexture3D::LoadFromFile(const String& filename) {
    if (filename.IsEmpty())
        return false;
    std::ifstream f((const char*)filename, std::ios::binary);
    if (not f)
        return false;

    uint32_t voxelCount = GridSize() * 4;
    uint32_t expectedBytes = voxelCount * sizeof(float);

    f.seekg(0, std::ios::end);
    std::streamoff fileSize = f.tellg();
    f.seekg(0, std::ios::beg);

    if (fileSize != std::streamoff(expectedBytes))
        return false;

    if (m_data.Length() != voxelCount)
        m_data.Resize(voxelCount);

    f.read(reinterpret_cast<char*>(m_data.Data()), expectedBytes);
    return f.good();
}


bool NoiseTexture3D::SaveToFile(const String& filename) const {
    if (filename.IsEmpty())
        return false;
    std::ofstream f((const char*)filename, std::ios::binary | std::ios::trunc);
    if (not f)
        return false;

    uint32_t voxelCount = GridSize() * 4u;
    uint32_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.Data()), bytes);
    return f.good();
}

// =================================================================================================

static float Amp(float v) {
    return 0.5f + 0.5f * cos(v * PI);
}


static float Amp2(float v) {
    return Amp(Amp(v));
}


bool CloudNoiseTexture::Allocate(int gridSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_gridSize = gridSize;
    m_data.Resize(size_t(gridSize) * gridSize * gridSize);
    texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize, 1, 0, 0);
    return true;
}


bool CloudNoiseTexture::Create(int gridSize, const NoiseParams& params, String noiseFilename, bool compute) {
    if (not Texture::Create())
        return false;
    m_type = TextureType::Texture3D;
    if (not Allocate(gridSize))
        return false;
    m_params = params;
    if (LoadFromFile(noiseFilename)) {
        if (not compute)
            return true;
        std::filesystem::path _p{ noiseFilename.GetStr() };
        Compute(_p.parent_path().string());
        SaveToFile(noiseFilename);
    }
#if 0
    float* data = m_data.Data();
    float minVal = 1e6f, maxVal = 0.0f;
    for (uint32_t i = m_gridSize * m_gridSize * m_gridSize; i; --i, ++data) {
        float n = *data;
        if (minVal > n) minVal = n;
        if (maxVal < n) maxVal = n;
    }
#endif
    Deploy();
    return true;
}


void CloudNoiseTexture::Compute(String textureFolder) {
    CloudNoise generator;

#if 1
    m_params.normalize = 1 + 2 + 4 + 8;
    NoiseTexture3D rgbaNoise;
    rgbaNoise.Create({ m_gridSize, m_gridSize, m_gridSize }, m_params, textureFolder + "/cloudnoise-rgba.bin", false);

    float* rgbaData = rgbaNoise.GetData().Data();
    float* data = m_data.Data();
    uint32_t dataSize = m_gridSize * m_gridSize * m_gridSize;

    for (int i = dataSize; i; --i) {
        float perlin = Amp(rgbaData[0]);
        perlin *= perlin;
#if CLOUD_STRUCTURE == 0
        float worley = Amp2(rgbaData[1]) * 0.625f + Amp2(rgbaData[2]) * 0.25f + Amp2(rgbaData[3]) * 0.125f;
#elif CLOUD_STRUCTURE == 1
        float worley = Amp2(rgbaData[1]) * 0.5f   + Amp2(rgbaData[2]) * 0.3f  + Amp2(rgbaData[3]) * 0.2f;
#else
        float worley = Amp2(rgbaData[1]) * 0.65f  + Amp2(rgbaData[2]) * 0.25f + Amp2(rgbaData[3]) * 0.1f;
#endif
        *data++ = generator.Remap(perlin, Amp2(worley) - 1.0f, 1.0f, 0.0f, 1.0f);
        rgbaData += 4;
    }

    rgbaNoise.GetData().Reset();
#else
    Vector3f p;
    for (int z = 0; z < m_gridSize; ++z) {
        p.z = (float(z) + 0.5f) / float(m_gridSize);
        for (int y = 0; y < m_gridSize; ++y) {
            p.y = (float(y) + 0.5f) / float(m_gridSize);
            for (int x = 0; x < m_gridSize; ++x) {
                p.x = (float(x) + 0.5f) / float(m_gridSize);
                Vector4f noise = generator.Compute(p);
                float perlin = Amp(noise.x);
                perlin *= perlin;
                float worley = Amp2(noise.y) * 0.625f + Amp2(noise.z) * 0.125f + Amp2(noise.w) * 0.25f;
                *m_data.Data()++ = generator.Remap(perlin, Amp2(worley) - 1.0f, 1.0f, 0.0f, 1.0f);
            }
        }
    }
#endif
}


void CloudNoiseTexture::SetParams(bool /*enforce*/) {
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Linear;
    m_sampling.magFilter = GfxFilterMode::Linear;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


bool CloudNoiseTexture::Deploy(int) {
    if (m_data.IsEmpty())
        return false;

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    // Single-channel float32 → R32_FLOAT
    constexpr DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT;
    constexpr uint32_t    stride = 4;

    m_resource = Upload3DTextureData(device, m_gridSize, m_gridSize, m_gridSize, fmt, stride, m_data.Data());
    if (not m_resource)
        return false;
    if (not CreateSRV3D(m_handle, m_resource.Get(), fmt))
        return false;

    m_isDeployed = true;
    return true;
}


bool CloudNoiseTexture::LoadFromFile(const String& filename) {
    return m_data.LoadFromFile(filename, size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize));
}


bool CloudNoiseTexture::SaveToFile(const String& filename) const {
    return m_data.SaveToFile(filename);
}


static bool IsPowerOfTwo(int n) {
    return (n > 0) and ((n & (n - 1)) == 0);
}


void CloudNoiseTexture::DownSample(float* src, int srcEdgeLen, float* dest, int destEdgeLen) {
    if ((src == nullptr) or (dest == nullptr))
        return;
    if (not IsPowerOfTwo(srcEdgeLen))
        return;
    if (not IsPowerOfTwo(destEdgeLen))
        return;
    if (destEdgeLen >= srcEdgeLen)
        return;

    const int ratio = srcEdgeLen / destEdgeLen;
    const size_t srcRow = size_t(srcEdgeLen);
    const size_t srcSlice = srcRow * srcRow;
    const size_t destRow = size_t(destEdgeLen);
    const size_t destSlice = destRow * destRow;

    for (int zd = 0; zd < destEdgeLen; ++zd) {
        const int sz0 = zd * ratio;
        for (int yd = 0; yd < destEdgeLen; ++yd) {
            const int sy0 = yd * ratio;
            for (int xd = 0; xd < destEdgeLen; ++xd) {
                const int sx0 = xd * ratio;
                float maxVal = src[size_t(sz0) * srcSlice + size_t(sy0) * srcRow + size_t(sx0)];
                for (int iz = 0; iz < ratio; ++iz) {
                    const size_t zOff = size_t(sz0 + iz) * srcSlice;
                    for (int iy = 0; iy < ratio; ++iy) {
                        const size_t yOff = size_t(sy0 + iy) * srcRow;
                        for (int ix = 0; ix < ratio; ++ix) {
                            const float v = src[zOff + yOff + size_t(sx0 + ix)];
                            if (maxVal < v)
                                maxVal = v;
                        }
                    }
                }
                dest[size_t(zd) * destSlice + size_t(yd) * destRow + size_t(xd)] = maxVal;
            }
        }
    }
}


void CloudNoiseTexture::ToMaxMip(CloudNoiseTexture* mipTex) {
    if (mipTex == nullptr)
        return;
    DownSample(m_data.Data(), m_gridSize, mipTex->m_data.Data(), mipTex->m_gridSize);
}


CloudNoiseTexture* CloudNoiseTexture::ToMaxMip(int mipSize, String noiseFilename) {
    if (not IsPowerOfTwo(m_gridSize))
        return nullptr;
    if (not IsPowerOfTwo(mipSize))
        return nullptr;
    if (mipSize >= m_gridSize)
        return nullptr;

    CloudNoiseTexture* mipTex = new CloudNoiseTexture();
    if (mipTex == nullptr)
        return nullptr;

    if (not mipTex->Create(mipSize, m_params, noiseFilename, false)) {
        delete mipTex;
        return nullptr;
    }

    ToMaxMip(mipTex);
    if (not mipTex->Deploy()) {
        delete mipTex;
        return nullptr;
    }
    mipTex->SaveToFile(noiseFilename);
    return mipTex;
}

// =================================================================================================

bool BlueNoiseTexture::Allocate(void) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf)
        return false;
    if (not m_buffers.Append(texBuf)) {
        delete texBuf;
        return false;
    }
    m_data.Resize(BufferSize());
    texBuf->m_info = TextureBuffer::BufferInfo(m_gridSize.x, m_gridSize.y * 64, 1, 0, 0);
    Validate();
    return true;
}


bool BlueNoiseTexture::Create(String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = TextureType::Texture3D;
    if (not Allocate())
        return false;
    if (not LoadFromFile(noiseFilename)) {
        std::filesystem::path _p{ noiseFilename.GetStr() };
        Compute(_p.parent_path().string());
        SaveToFile(noiseFilename);
    }
    Deploy();
    return true;
}


void BlueNoiseTexture::Compute(String textureFolder) {
    uint32_t layerSize = m_gridSize.x * m_gridSize.y;
    for (int i = 0; i < 64; ++i) {
        String filename = textureFolder + "/bluenoise/stbn_scalar_2Dx1Dx1D_128x128x64x1_" + String(i) + ".png";
        SDL_Surface* image = IMG_Load(filename.Data());
        if (image) {
            std::memcpy(m_data.Data(layerSize * i), image->pixels, layerSize);
            SDL_FreeSurface(image);
        }
    }
}


void BlueNoiseTexture::SetParams(bool /*enforce*/) {
    m_hasParams = true;
    m_sampling.minFilter = GfxFilterMode::Nearest;
    m_sampling.magFilter = GfxFilterMode::Nearest;
    m_sampling.mipMode = GfxMipMode::None;
    m_sampling.wrapU = GfxWrapMode::Repeat;
    m_sampling.wrapV = GfxWrapMode::Repeat;
    m_sampling.wrapW = GfxWrapMode::Repeat;
    m_sampling.compareFunc = GfxOperations::CompareFunc::Always;
    m_sampling.maxAnisotropy = 1.0f;
}


bool BlueNoiseTexture::Deploy(int) {
    if (m_data.IsEmpty())
        return false;

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    constexpr DXGI_FORMAT fmt = DXGI_FORMAT_R8_UNORM;
    constexpr uint32_t    stride = 1;

    m_resource = Upload3DTextureData(device, 128, 128, 64, fmt, stride, m_data.Data());
    if (not m_resource)
        return false;
    if (not CreateSRV3D(m_handle, m_resource.Get(), fmt))
        return false;

    m_isDeployed = true;
    return true;
}


bool BlueNoiseTexture::LoadFromFile(const String& filename) {
    return m_data.LoadFromFile(filename, BufferSize());
}


bool BlueNoiseTexture::SaveToFile(const String& filename) {
    return m_data.SaveToFile(filename);
}

// =================================================================================================
