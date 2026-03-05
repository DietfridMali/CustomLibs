// GLBReader.h
#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>

#include "tiny_gltf.h"
#include "string.hpp"
#include "vector.hpp"
#include "array.hpp"
#include "matrix.hpp"
#include "list.hpp"

// =================================================================================================

class GLBReader {
public:
    struct ShapeKeySet {
        String name;
        AutoArray<Vector3f> deltas;
    };

    struct MeshData {
        AutoArray<Vector3f> vertices;
        AutoArray<Vector4f> colors;
        AutoArray<Vector3f> triNormals;
        List<ShapeKeySet> shapeKeys;
    };

public:
    bool Load(const String& filename);

    MeshData& Data() { return m_data; }

    const MeshData& Data() const { return m_data; }

private:
    struct PrimitiveInput {
        AutoArray<Vector3f> basePos;
        AutoArray<uint32_t> indices;
        AutoArray<AutoArray<Vector3f>> morphPos; // [target][vertex]
        Vector4f baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        int32_t targetCount{ 0 };
        int32_t triCount{ 0 };
    };

private:
    static Matrix4f NodeLocalMatrix(const tinygltf::Node& node);

    static bool ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, AutoArray<Vector3f>& out);

    static bool ReadAccessorIndicesU32(const tinygltf::Model& model, int accessorIndex, AutoArray<uint32_t>& out);

    static Vector4f PrimitiveBaseColor(const tinygltf::Model& model, int materialIndex);

    void CheckShapeKeyCount(int32_t targetCount);

    bool AppendFromNode(int nodeIndex, Matrix4f parentM);

    bool AppendMesh(int meshIndex, Matrix4f worldM);

private:
    bool AppendPrimitive(tinygltf::Primitive& prim, Matrix4f worldM);

    bool ValidateTriangles(tinygltf::Primitive& prim);

    bool LoadPositions(tinygltf::Primitive& prim, PrimitiveInput& in);

    bool LoadMorphTargets(tinygltf::Primitive& prim, PrimitiveInput& in);

    bool LoadIndices(tinygltf::Primitive& prim, PrimitiveInput& in);

    void ReserveOutput(const PrimitiveInput& in);

    void BuildShapeKeyPointers(AutoArray<ShapeKeySet*>& keyPtrs);

    bool AppendTriangles(const PrimitiveInput& in, Matrix4f worldM, AutoArray<ShapeKeySet*>& keyPtrs);

private:
    tinygltf::Model m_model;

    MeshData m_data;
};

// =================================================================================================
