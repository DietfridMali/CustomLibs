
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>

#include "noisetexture.h"
#include "base_renderer.h"
//#include "../../../../projects/smiley-battle/include/progressindicator.h"
#include "conversions.hpp"

#include "noise.h"

using namespace Noise;

#define NORMALIZE_NOISE 0

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
    texBuf->m_info = TextureBuffer::BufferInfo(gridDimensions.x, gridDimensions.y * gridDimensions.z, 1, GL_R16F, GL_RED);
    HasBuffer() = true;
    return true;
}


bool NoiseTexture3D::Create(Vector3i gridDimensions, const NoiseParams& params, String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = GL_TEXTURE_3D;
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
    SimpleArray<uint32_t, 101> d[4];
    for (int i = 0; i < 4; ++i)
        d[i].fill(0);
    for (uint32_t i = m_gridDimensions.x * m_gridDimensions.y * m_gridDimensions.z; i; --i) {
        Vector4f noise;
#if 1
        for (int i = 0; i < 4; ++i)
            ++d[i][int(data[i] * 100)];
#endif
        noise.x = *data++;
        noise.y = *data++;
        noise.z = *data++;
        noise.w = *data++;
        minVals.Minimize(noise);
        maxVals.Maximize(noise);
    }

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

    m_params.normalize = 0; // 1 + 2 + 4 + 8;

    Vector4f noise;
    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ 0.0f, 0.0f, 0.0f, 0.0f };
#ifdef _DEBUG
    int belowCoverage = 0;
#endif

    CloudNoise generator;

    int i = 0;
    Vector3f p;
    for (int z = 0; z < m_gridDimensions.z; ++z) {
        p.y = (float(z) + 0.5f) / float(m_gridDimensions.z);
        for (int y = 0; y < m_gridDimensions.y; ++y) {
            p.z = (float(y) + 0.5f) / float(m_gridDimensions.y);
            for (int x = 0; x < m_gridDimensions.x; ++x) {
                p.x = (float(x) + 0.5f) / float(m_gridDimensions.x);
                noise = generator.Compute(p);
                data[i++] = Saturate(noise.x);
                data[i++] = Saturate(noise.y);
                data[i++] = Saturate(noise.z);
                data[i++] = Saturate(noise.a);
                if (m_params.normalize) {
                    minVals.Minimize(noise);
                    maxVals.Maximize(noise);
                }
#ifdef _DEBUG
                if (noise.x < 0.3125)
                    ++belowCoverage;
#endif
            }
        }
    }
#ifdef _DEBUG
    float minVal = 1e6f;
    float maxVal = 0.0f;
#endif
    int dataSize = i;
    if (m_params.normalize) {
#ifdef _DEBUG
        belowCoverage = 0;
#endif
        for (int i = 0; i < dataSize; ) {
            if (m_params.normalize & 1) {
                data[i] = Conversions::Normalize(data[i], minVals.x, maxVals.x);
#ifdef _DEBUG
                if (minVal > data[i])
                    minVal = data[i];
                if (maxVal < data[i])
                    maxVal = data[i];
                if (data[i] < 0.3125)
                    ++belowCoverage;
#endif
            }
            ++i;
            if (m_params.normalize & 2)
                data[i] = Conversions::Normalize(data[i], minVals.y, maxVals.y);
            ++i;
            if (m_params.normalize & 4)
                data[i] = Conversions::Normalize(data[i], minVals.z, maxVals.z);
            ++i;
            if (m_params.normalize & 8)
                data[i] = Conversions::Normalize(data[i], minVals.w, maxVals.w);
            ++i;
        }
    }
#if 0
    // modulate base noise with detail noise
    for (int i = 0; i < dataSize; i += 4) {
        data[i] = Modulate(data[i], data[i + 1], data[i + 2], data[i + 3]);
        if (minVal > data[i])
            minVal = data[i];
        if (maxVal < data[i])
            maxVal = data[i];
    }
    if (maxVal - minVal < 0.999f) {
        for (int i = 0; i < dataSize; i += 4) {
            data[i] = Conversions::Normalize(data[i], minVal, maxVal);
        }
    }
#endif
}


void NoiseTexture3D::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}


void NoiseTexture3D::Deploy(int) {
    if (Bind()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, m_gridDimensions.x, m_gridDimensions.y, m_gridDimensions.z, 0, GL_RGBA, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        glGenerateMipmap(GL_TEXTURE_3D);
        Release();
    }
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
    texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize, 1, GL_R16F, GL_RED);
    HasBuffer() = true;
    return true;
}


bool CloudNoiseTexture::Create(int gridSize, const NoiseParams& params, String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = GL_TEXTURE_3D;
    if (not Allocate(gridSize))
        return false;
    m_params = params;
    if (LoadFromFile(noiseFilename)) {
    }
    else {
        Compute();
        SaveToFile(noiseFilename);
    }
    float* data = m_data.Data();
    float minVal = 1e6f, maxVal = 0.0f;
    for (uint32_t i = m_gridSize * m_gridSize * m_gridSize; i; --i, ++data) {
        float n = *data;
        if (minVal > n)
            minVal = n;
        if (maxVal < n)
            maxVal = n;
    }

    SimpleArray<uint32_t, 101> distribution1;
    distribution1.fill(0);
    SimpleArray<uint32_t, 101> distribution2;
    distribution2.fill(0);
    SimpleArray<uint32_t, 101> distribution3;
    distribution3.fill(0);

    data = m_data.Data();
    for (uint32_t i = m_gridSize * m_gridSize * m_gridSize; i; --i, ++data) {
        float n = Conversions::Normalize(*data, minVal, maxVal);
        *data = n * n;
#if 0
        ++distribution1[int(*data * 100)];
#endif
    }

    Deploy();
    return true;
}


void CloudNoiseTexture::Compute(void) {
    float* data = m_data.Data();
    size_t i = 0;
    float minVal = 1e6f, maxVal = 0.0f;

    m_params.normalize = 0;

    CloudNoise generator;

    Vector3f p;
    SimpleArray<int, 101> distribution;
    distribution.fill(0);
    for (int z = 0; z < m_gridSize; ++z) {
        //progressIndicator.Update(1);
        p.z = (float(z) + 0.5f) / float(m_gridSize);
        for (int y = 0; y < m_gridSize; ++y) {
            p.y = (float(y) + 0.5f) / float(m_gridSize);
            for (int x = 0; x < m_gridSize; ++x) {
                p.x = (float(x) + 0.5f) / float(m_gridSize);
                Vector4f noise = generator.Compute(p);
                float wfbm = noise.y * 0.625f + noise.z * 0.125f + noise.w * 0.25f;
                float n = generator.Remap(noise.x, wfbm - 1.0f, 1.0f, 0.0f, 1.0f);
                data[i++] = n;
                ++distribution[int(n * 100)];
                if (minVal > n)
                    minVal = n;
                if (maxVal < n)
                    maxVal = n;
            }
        }
    }

    if (m_params.normalize and (maxVal - minVal < 0.999f)) {
        for (; i; --i, ++data)
            *data = Conversions::Normalize(*data, minVal, maxVal);
    }
}


void CloudNoiseTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}


void CloudNoiseTexture::Deploy(int) {
    if (Bind()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, m_gridSize, m_gridSize, m_gridSize, 0, GL_RED, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        glGenerateMipmap(GL_TEXTURE_3D);
        Release();
    }
}


bool CloudNoiseTexture::LoadFromFile(const String& filename) {
    if (filename.IsEmpty())
        return false;
    std::ifstream f((const char*)filename, std::ios::binary);
    if (not f)
        return false;

    int voxelCount = size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize);
    int expectedBytes = voxelCount * sizeof(float);

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


bool CloudNoiseTexture::SaveToFile(const String& filename) const {
    if (filename.IsEmpty())
        return false;
    std::ofstream f((const char*)filename, std::ios::binary | std::ios::trunc);
    if (not f)
        return false;

    size_t voxelCount = size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize);
    size_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.Data()), bytes);
    return f.good();
}

// =================================================================================================
