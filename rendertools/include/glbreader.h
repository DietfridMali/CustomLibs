// GLBReader.h
#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <unordered_map>

// somewhere in exactly one .cpp before including tiny_gltf.h
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "tiny_gltf.h"

class GLBReader {
public:
    struct MeshData {
        AutoArray<Vector3f> vertices;
        AutoArray<Vector4f> colors;
        AutoArray<Vector3f> morph0;
        AutoArray<Vector3f> morph1;
        AutoArray<Vector3f> triNormals;
    };

public:
    bool Load(const std::string& filename);

    MeshData& Data() {
        return m_data;
    }

    const MeshData& Data() const {
        return m_data;
    }

private:
    static glm::mat4 NodeLocalMatrix(const tinygltf::Node& node);
    static void ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, std::vector<Vector3f>& out);
    static void ReadAccessorIndices(const tinygltf::Model& model, int accessorIndex, std::vector<uint32_t>& out);
    static Vector4f PrimitiveBaseColor(const tinygltf::Model& model, int materialIndex);

    void AppendMeshFromNode(int nodeIndex, glm::mat4 parentM);
    void AppendMesh(int meshIndex, glm::mat4 worldM);

private:
    tinygltf::Model m_model;
    MeshData m_data;
}; 

