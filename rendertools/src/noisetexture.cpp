
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
// Helpers

#if 0

// Periodischer Worley 3D (F1) + invertiert + fBm
float WorleyF1_Periodic(float X, float Y, float Z, int period, int cells, uint32_t seed) {
    float scale = float(cells) / float(period);
    float x = X * scale;
    float y = Y * scale;
    float z = Z * scale;
    int cx = int(floorf(x));
    int cy = int(floorf(y));
    int cz = int(floorf(z));
    float dmin = 1e9f;
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int ix = Wrap(cx + dx, cells);
                int iy = Wrap(cy + dy, cells);
                int iz = Wrap(cz + dz, cells);
                uint32_t h = Hash3D((uint32_t)ix, (uint32_t)iy, (uint32_t)iz, seed);
                float jx = R01(h);
                float jy = R01(h * 0x9e3779b9u + 0x7f4a7c15u);
                float jz = R01(h * 0x85ebca6bu + 0xc2b2ae35u);
                float fx = float(ix) + jx;
                float fy = float(iy) + jy;
                float fz = float(iz) + jz;
                float dxw = x - fx;
                float dyw = y - fy;
                float dzw = z - fz;
                dxw -= floorf(dxw / float(cells) + 0.5f) * float(cells);
                dyw -= floorf(dyw / float(cells) + 0.5f) * float(cells);
                dzw -= floorf(dzw / float(cells) + 0.5f) * float(cells);
                float d = sqrtf(dxw * dxw + dyw * dyw + dzw * dzw);
                if (d < dmin) dmin = d;
            }
    float invMax = 1.0f / 1.7320508075688772f;
    return std::clamp(dmin * invMax, 0.0f, 1.0f);
}


float WorleyFBM_Periodic(float X, float Y, float Z, int period, int cells0, int oct, float lac, float gain, uint32_t seed) {
    float a = 1.f;
    float sum = 0.f;
    float norm = 0.f;
    int cells = cells0;
    for (int i = 0; i < oct and cells <= period; i++) {
        float v = WorleyF1_Periodic(X, Y, Z, period, cells, seed + uint32_t(i * 733));
        sum += a * v;
        norm += a;
        a *= gain;
        cells = int(cells * lac);
    }
    if (norm <= 0.f) return 0.5f;
    return std::clamp(sum / norm, 0.0f, 1.0f);
}

float WorleyF1_Tiled(float u, float v, float w, int cells, uint32_t seed) {
    u -= floorf(u);
    v -= floorf(v);
    w -= floorf(w);
    float x = u * cells;
    float y = v * cells;
    float z = w * cells;
    int ix = int(floorf(x));
    int iy = int(floorf(y));
    int iz = int(floorf(z));
    float dmin = 1e9f;
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int cx = Wrap(ix + dx, cells);
                int cy = Wrap(iy + dy, cells);
                int cz = Wrap(iz + dz, cells);
                uint32_t h = Hash3D((uint32_t)cx, (uint32_t)cy, (uint32_t)cz, seed);
                float jx = R01(h);
                float jy = R01(h * 0x9e3779b9u + 0x7f4a7c15u);
                float jz = R01(h * 0x85ebca6bu + 0xc2b2ae35u);
                float fx = float(cx) + jx;
                float fy = float(cy) + jy;
                float fz = float(cz) + jz;
                float dxw = x - fx;
                float dyw = y - fy;
                float dzw = z - fz;
                dxw -= roundf(dxw / float(cells)) * float(cells);
                dyw -= roundf(dyw / float(cells)) * float(cells);
                dzw -= roundf(dzw / float(cells)) * float(cells);
                float d = sqrtf(dxw * dxw + dyw * dyw + dzw * dzw);
                if (d < dmin) dmin = d;
            }
    float invMax = 1.0f / 1.7320508075688772f;
    return std::clamp(dmin * invMax, 0.0f, 1.0f);
}


float WorleyFBM_Tiled(float u, float v, float w, int maxCells, int cells0, int oct, float lac, float gain, uint32_t seed) {
    float a = 1.f;
    float sum = 0.f;
    float norm = 0.f;
    int cells = cells0;
    for (int i = 0; i < oct && cells <= maxCells; i++) {
        sum += a * WorleyF1_Tiled(u, v, w, cells, seed + uint32_t(i * 733));
        norm += a;
        a *= gain;
        cells = int(cells * lac);
    }
    if (norm <= 0.f) return 0.5f;
    return std::clamp(sum / norm, 0.0f, 1.0f);
}

#endif

// =================================================================================================
/* für volumetrische Wolken:
    NoiseTexture3D shapeTex3D, detailTex3D;

    Noise3DParams pShape;
    pShape.seed = 0xA5A5A5u;
    pShape.base = 2.032f;   // grobe Strukturen
    pShape.lac = 2.3f;
    pShape.oct = 5;
    pShape.warp = 0.08f;

    Noise3DParams pDetail = pShape;
    pDetail.seed = 0x9e3779b9u;
    pDetail.base = 0.6f;    // höhere Frequenz
    pDetail.lac = 2.8f;
    pDetail.oct = 4;
    pDetail.warp = 0.05f;

    shapeTex3D.Create(128, pShape);   // oder 256, wenn Speicher reicht
    detailTex3D.Create(32,  pDetail);
*/
// --- Simplex periodisch (wie zuletzt bei dir) ---
// -------------------------------------------------------------------------------------------------

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


void NoiseTexture3D::ComputeNoise(void) {
    float* data = m_data.Data();

    const float cellSize = float(m_gridSize) / float(m_params.cellsPerAxis); // Anzahl Perlin-Zellen pro Kachel

#if 0 // simple perlin

    struct PerlinFunctor {
        float operator()(float x, float y, float z) const {
            return Perlin::Noise(x, y, z); // ~[-1,1]
        }
    };

    PerlinFunctor noiseFn{};
    FBM<PerlinFunctor> fbm(noiseFn, m_params.fbmParams);

    Vector4f noise;
    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ 0.0f, 0.0f, 0.0f, 0.0f };
    int belowThreshold = 0;

    int i = 0;
    for (int z = 0; z < m_gridSize; ++z) {
        float w = float(z) / cellSize;
        for (int y = 0; y < m_gridSize; ++y) {
            float v = float(y) / cellSize;
            for (int x = 0; x < m_gridSize; ++x) {
                float u = float(x) / cellSize;
#if 0
                noise.x = (1.0f + Perlin(u, v, w)) * 0.5f; // fbm.Value(u, v, w); // [0,1]
#else
                noise.x = fbm.Value(u, v, w);
#endif
                minVals.Minimize(noise);
                maxVals.Maximize(noise);

                data[i++] = std::clamp(noise.x, 0.0f, 1.0f);
                if (noise.x < 0.3125)
                    ++belowThreshold;
                data[i++] = 0.0f; // std::clamp(noise.y, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.z, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.w, 0.0f, 1.0f);
            }
        }
    }

#else

    std::vector<int> perm;
    BuildPermutation(perm, m_params.cellsPerAxis, m_params.seed);
    ImprovedPerlinFunctor noiseFn{ perm, m_params.cellsPerAxis };
    FBM<ImprovedPerlinFunctor> fbm(noiseFn, m_params.fbmParams);

    Vector4f noise;
    Vector4f minVals{ 1e6f, 1e6f, 1e6f, 1e6f };
    Vector4f maxVals{ 0.0f, 0.0f, 0.0f, 0.0f };

    int i = 0;
    for (int z = 0; z < m_gridSize; ++z) {
        float w = float(z) / float(m_gridSize) * m_params.cellsPerAxis;
        for (int y = 0; y < m_gridSize; ++y) {
            float v = float(y) / float(m_gridSize) * m_params.cellsPerAxis;
            for (int x = 0; x < m_gridSize; ++x) {
                float u = float(x) / float(m_gridSize) * m_params.cellsPerAxis;

                noise.x = fbm.Value(u, v, w); // [0,1]
                minVals.Minimize(noise);
                maxVals.Maximize(noise);

                data[i++] = std::clamp(noise.x, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.y, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.z, 0.0f, 1.0f);
                data[i++] = 0.0f; // std::clamp(noise.w, 0.0f, 1.0f);
            }
        }
    }

#endif

#   if 0
    for (int z = 0; z < m_gridSize; ++z) {
        for (int y = 0; y < m_gridSize; ++y) {
            for (int x = 0; x < m_gridSize; ++x) {
                noise.x = PerlinNoise(float(x) / cellSize, float(y) / cellSize, float(z) / cellSize);
                // Perlin-fBm für Basis
                float perlin = 0.0f;
                float a = m_params.initialGain;
                float f = m_params.baseFrequency * float(perlinPeriod);

                for (int o = 0; o < m_params.octaves; ++o) {
                    //float n = PerlinNoise3D(X * f, Y * f, Z * f, 8, m_params.seed ^ 0xA5A5A5u); // ~[-1,1]
                    float n = PerlinNoise(X * f, Y * f, Z * f, perm, perlinPeriod);
                    perlin += a * n;
                    a *= m_params.gain;
                    f *= m_params.lacunarity;
                }

                perlin = 0.5f + 0.5f * (perlin / perlinNorm);
                perlin = std::clamp(perlin, 0.0f, 1.0f);

                // Worley-fBm für Kanäle
                //noise.x = WorleyF1_Periodic(X, Y, Z, m_gridSize, C_R, m_params.seed ^ 0x51u);
                noise.x = perlin; // *(1.0f - noise.x);
#   if 0
                noise.x = std::pow(noise.x, 0.5f);
                noise.x = std::clamp(noise.x, 0.0f, 1.0f);
#   endif
#   if 0
#       if 1
                noise.y = WorleyFBM_Periodic(X, Y, Z, m_gridSize, C_G, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0x1337u);
                noise.z = WorleyFBM_Periodic(X, Y, Z, m_gridSize, C_B, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xBEEFu);
                noise.w = WorleyFBM_Periodic(X, Y, Z, m_gridSize, C_A, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xCAFEu);
#      else
                noise.y = WorleyFBM_Tiled(X, Y, Z, m_gridSize, C_G, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0x1337u);
                noise.z = WorleyFBM_Tiled(X, Y, Z, m_gridSize, C_B, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xBEEFu);
                noise.w = WorleyFBM_Tiled(X, Y, Z, m_gridSize, C_A, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed ^ 0xCAFEu);
#       endif
#   endif
                minVals.Minimize(noise);
                maxVals.Maximize(noise);

                data[idx++] = std::clamp(noise.x, 0.0f, 1.0f);
                data[idx++] = 0.0f; // std::clamp(noise.y, 0.0f, 1.0f);
                data[idx++] = 0.0f; // std::clamp(noise.z, 0.0f, 1.0f);
                data[idx++] = 0.0f; // std::clamp(noise.w, 0.0f, 1.0f);
            }
        }
    }
#endif

#if 1
    if (m_params.normalize) {
        int h = i;
        for (int i = 0; i < h; ) {
            if (m_params.normalize & 1)
                Conversions::Normalize(data[i], minVals.x, maxVals.x);
            ++i;
            if (m_params.normalize & 2)
                Conversions::Normalize(data[i++], minVals.y, maxVals.y);
            ++i;
            if (m_params.normalize & 4)
                Conversions::Normalize(data[i++], minVals.z, maxVals.z);
            ++i;
            if (m_params.normalize & 8)
                Conversions::Normalize(data[i++], minVals.w, maxVals.w);
            ++i;
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
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, m_gridSize, m_gridSize, m_gridSize, 0, GL_RGBA, GL_FLOAT, reinterpret_cast<const void*>(m_data.Data())
        );
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

#if 0

void CloudVolume3D::Compute() {
    float* data = m_data.Data();
    size_t idx = 0;

    for (int z = 0; z < m_gridSize; ++z) {
        for (int y = 0; y < m_gridSize; ++y) {
            for (int x = 0; x < m_gridSize; ++x) {
                data[idx++] = PerlinFBM3_Periodic(float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f, m_gridSize, m_params.octaves, m_params.lacunarity, m_params.gain, m_params.seed);
            }
        }
    }
}

#else

void CloudVolume3D::Compute(void) {
    float* data = m_data.Data();

    SimplexAshimaFunctor noiseFn{};
    FBM<SimplexAshimaFunctor> fbm(noiseFn, m_params.fbmParams);


    size_t i = 0;
    float minVal = 1e6f, maxVal = 0.0f;
    for (int z = 0; z < m_gridSize; ++z) {
        float w = (float(z) + 0.5f) / float(m_gridSize);
        for (int y = 0; y < m_gridSize; ++y) {
            float v = (float(y) + 0.5f) / float(m_gridSize);
            for (int x = 0; x < m_gridSize; ++x) {
                float u = (float(x) + 0.5f) / float(m_gridSize);
                float n = fbm.Value(u, v, w);
                data[i++] = n;
                if (minVal > n)
                    minVal = n;
                if (maxVal < n)
                    maxVal = n;
            }
        }
    }

    if (m_params.normalize and (maxVal > 0.0f) and (maxVal < 0.999f)) {
        for (; i; --i, ++data)
            Conversions::Normalize(*data, minVal, maxVal);
    }
}

#endif

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
