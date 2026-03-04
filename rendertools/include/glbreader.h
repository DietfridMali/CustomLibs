// GLBReader.h
#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>

#include "tiny_gltf.h"
#include "glm_vector.hpp"
#include "Matrix4f.hpp"
#include "List.hpp"

class GLBReader {
public:
    struct ShapeKeySet {
        std::string name;
        AutoArray<Vector3f> deltas; // triangle soup: same length as vertices
    };

    struct MeshData {
        AutoArray<Vector3f> vertices;     // triangle soup: 3 * triCount
        AutoArray<Vector4f> colors;       // 3 * triCount
        AutoArray<Vector3f> triNormals;   // 1 * triCount (face normal)
        List<ShapeKeySet> shapeKeys;      // N sets, each has 3 * triCount deltas
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
    static Matrix4f NodeLocalMatrix(const tinygltf::Node& node);
    static void ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, std::vector<Vector3f>& out);
    static void ReadAccessorIndicesU32(const tinygltf::Model& model, int accessorIndex, std::vector<uint32_t>& out);
    static Vector4f PrimitiveBaseColor(const tinygltf::Model& model, int materialIndex);

    void EnsureShapeKeyCount(int32_t targetCount);
    void AppendFromNode(int nodeIndex, Matrix4f parentM);
    void AppendMesh(int meshIndex, Matrix4f worldM);

private:
    tinygltf::Model m_model;
    MeshData m_data;
};
