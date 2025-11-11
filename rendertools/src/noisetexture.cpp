
// --- tileable fBM noise (periodisch in X/Y) ---------------------------------
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>

#include "noisetexture.h"
#include "base_renderer.h"
#include "conversions.hpp"

#include "noise.h"

using namespace Noise;

#define NORMALIZE_NOISE 0

// =================================================================================================

bool NoiseTexture3D::Allocate(int gridSize) {
    TextureBuffer* texBuf = new TextureBuffer();
    if (not texBuf) 
        return false;
    if (not m_buffers.Append(texBuf)) { 
        delete texBuf; 
        return false; 
    }
    m_gridSize = gridSize;
    m_data.Resize(gridSize * gridSize * gridSize * 4);
    texBuf->m_info = TextureBuffer::BufferInfo(gridSize, gridSize, 1, GL_R16F, GL_RED);
    HasBuffer() = true;
    return true;
}


bool NoiseTexture3D::Create(int gridSize, const NoiseParams& params, String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = GL_TEXTURE_3D;
    if (not Allocate(gridSize))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        ComputeNoise();
        SaveToFile(noiseFilename);
    }
    Deploy();
    return true;
}


static float Modulate(float shape, float detail) {
    const float sharpness = 0.6f;                    // Detailstärke 0..1
    const float fuzziness = 0.3f;
    float lumps = powf(detail, 1.5f);
    float erosion = powf(1.0f - detail, 1.5f);
    float density = shape - fuzziness * erosion;
    density *= (1.0f - sharpness) + sharpness * lumps;
    return 1.0f - density;
}


void NoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.Data();

    const float cellSize = float(m_gridSize) / float(m_params.cellsPerAxis); // Anzahl Perlin-Zellen pro Kachel
    m_params.normalize = 1 + 2 + 4;
    
    NoiseParams shapeParams;
    shapeParams.fbmParams.perturb = 1;
    shapeParams.normalize = 1;

    PerlinNoise::Setup(m_params.cellsPerAxis);
    PerlinFunctor shapeNoise{ };
    FBM<PerlinFunctor> shapeFbm(shapeNoise, shapeParams.fbmParams);

    NoiseParams detailParams;
    detailParams.fbmParams.octaves = 1;
    detailParams.fbmParams.initialGain = 1.0f;
    detailParams.fbmParams.gain = 1.0f;
    detailParams.fbmParams.frequency = 4.0f;   
    detailParams.fbmParams.lacunarity = 2.0f;  
    detailParams.fbmParams.perturb = 1;
    detailParams.fbmParams.normalize = false;

    WorleyFunctor detailNoise{ m_params.cellsPerAxis };
    FBM<WorleyFunctor> detailFbm(detailNoise, detailParams.fbmParams);

    detailParams.fbmParams.frequency = 8.0f;
    detailParams.fbmParams.lacunarity = 4.0f;

    WorleyFunctor fineDetailNoise{ m_params.cellsPerAxis };
    FBM<WorleyFunctor> fineDetailFbm(fineDetailNoise, detailParams.fbmParams);

    Vector4f noise;
    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ 0.0f, 0.0f, 0.0f, 0.0f };
#ifdef _DEBUG
    int belowCoverage = 0;
#endif

    int i = 0;
    Vector3f p;
    float cellScale = float(m_params.cellsPerAxis) / float(m_gridSize - 1);
    for (int z = 0; z < m_gridSize; ++z) {
        p.z = (float(z) + 0.5f) * cellScale;
        for (int y = 0; y < m_gridSize; ++y) {
            p.y = (float(y) + 0.5f) * cellScale;
            for (int x = 0; x < m_gridSize; ++x) {
                p.x = (float(x) + 0.5f) * cellScale;
                noise.x = shapeFbm.Value(p);      // [0,1]
                noise.y = 1.0f - detailFbm.Value(p);      // [0,1], Peaks = Knubbel
                noise.z = 1.0f - fineDetailFbm.Value(p);      // [0,1], Peaks = Knubbel
                noise.a = noise.x;
                data[i++] = std::clamp(noise.x, 0.0f, 1.0f);
                data[i++] = std::clamp(noise.y, 0.0f, 1.0f);
                data[i++] = std::clamp(noise.z, 0.0f, 1.0f);
                data[i++] = std::clamp(noise.a, 0.0f, 1.0f);
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
        belowCoverage = 0;
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

    // modulate base noise with detail noise
    for (int i = 0; i < dataSize; i += 4) {
        data[i] = Modulate(data[i], 1.0f - data[i+1]);
    }
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
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, m_gridSize, m_gridSize, m_gridSize, 0, GL_RGBA, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
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

    int voxelCount = size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize) * 4u;
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


bool NoiseTexture3D::SaveToFile(const String& filename) const {
    if (filename.IsEmpty())
        return false;
    std::ofstream f((const char*)filename, std::ios::binary | std::ios::trunc);
    if (not f)
        return false;

    size_t voxelCount = size_t(m_gridSize) * size_t(m_gridSize) * size_t(m_gridSize) * 4u;
    size_t bytes = voxelCount * sizeof(float);

    if (m_data.Length() != voxelCount)
        return false;

    f.write(reinterpret_cast<const char*>(m_data.Data()), bytes);
    return f.good();
}

// =================================================================================================

bool CloudVolume3D::Allocate(int gridSize) {
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


bool CloudVolume3D::Create(int gridSize, const NoiseParams& params, String noiseFilename) {
    if (not Texture::Create())
        return false;
    m_type = GL_TEXTURE_3D;
    if (not Allocate(gridSize))
        return false;
    m_params = params;
    if (not LoadFromFile(noiseFilename)) {
        Compute();
        SaveToFile(noiseFilename);
    }
    Deploy();
    return true;
}


void CloudVolume3D::Compute(void) {
    SimplexAshimaGLSLFunctor noiseFn{};
    FBM<SimplexAshimaGLSLFunctor> fbm(noiseFn, m_params.fbmParams);

    float* data = m_data.Data();
    size_t i = 0;
    float minVal = 1e6f, maxVal = 0.0f;
    int belowCoverage = 0;
    int isZero = 0;

    Vector3f p;
    float cellScale = float(m_params.cellsPerAxis) / float(m_gridSize - 1);
    for (int z = 0; z < m_gridSize; ++z) {
        p.z = float(z) * cellScale;
        for (int y = 0; y < m_gridSize; ++y) {
            p.y = float(y) * cellScale;
            for (int x = 0; x < m_gridSize; ++x) {
                p.x = float(x) * cellScale;
                float n = fbm.Value(p);
                data[i++] = n;
                if (minVal > n)
                    minVal = n;
                if (maxVal < n)
                    maxVal = n;
                if (n < 0.3125)
                    ++belowCoverage;
                if (n == 0)
                    ++isZero;
            }
        }
    }

    if (m_params.normalize and (maxVal - minVal < 0.999f)) {
        for (; i; --i, ++data)
            Conversions::Stretch(*data, minVal, maxVal);
    }
}


void CloudVolume3D::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}


void CloudVolume3D::Deploy(int) {
    if (Bind()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, m_gridSize, m_gridSize, m_gridSize, 0, GL_RED, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data()));
        SetParams(false);
        glGenerateMipmap(GL_TEXTURE_3D);
        Release();
    }
}


bool CloudVolume3D::LoadFromFile(const String& filename) {
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


bool CloudVolume3D::SaveToFile(const String& filename) const {
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
