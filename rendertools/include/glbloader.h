// GLBLoader.h
#pragma once

#include <cstdint>
#include <cstdio>

#include "tiny_gltf.h"
#include "string.hpp"
#include "vector.hpp"
#include "array.hpp"
#include "matrix.hpp"
#include "list.hpp"
#include "colordata.h"

// =================================================================================================

class GLBLoader {
public:
    struct ShapeKeySet {
        String name;
        AutoArray<Vector3f> deltas; // triangle soup: same length as vertices
    };

    struct MeshData {
        AutoArray<Vector3f>     vertices;   // triangle soup: 3 * triCount
        AutoArray<RGBAColor>    colors;     // 3 * triCount
        AutoArray<Vector3f>     normals;    // 1 * triCount
        List<ShapeKeySet>       shapeKeys;  // N sets, each has 3 * triCount deltas
    };

public:
    bool Load(const String& filename);

    inline MeshData& Data() { 
        return m_data; 
    }

    inline const MeshData& Data() const { 
        return m_data; 
    }

private:
    struct PrimitiveInput {
        AutoArray<Vector3f>             basePos;
        AutoArray<uint32_t>             indices;
        AutoArray<AutoArray<Vector3f>>  morphPos; // [target][vertex]
        RGBAColor                       baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        int32_t                         targetCount{ 0 };
        int32_t                         triCount{ 0 };
    };

private:
    static Matrix4f NodeLocalMatrix(const tinygltf::Node& node);

    static bool ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, AutoArray<Vector3f>& out);

    static bool ReadAccessorIndicesU32(const tinygltf::Model& model, int accessorIndex, AutoArray<uint32_t>& out);

    static Vector4f PrimitiveBaseColor(const tinygltf::Model& model, int materialIndex);

    void CheckShapeKeyCount(int32_t targetCount);

    bool AppendFromNode(int nodeIndex, Matrix4f parentM);

    bool AppendMesh(int meshIndex, Matrix4f worldM);

public:
	inline AutoArray<Vector3f>& GetVertices(void) noexcept {
        return m_data.vertices; 
    }

    inline AutoArray<Vector3f>& GetNormals(void) noexcept {
        return m_data.normals;
    }

    inline AutoArray<RGBAColor>& GetColors(void) noexcept {
        return m_data.colors;
    }

    inline int HasShapeKeys(void) const noexcept {
        return m_data.shapeKeys.Length();
	}

    inline AutoArray<Vector3f>& GetShapeKeys(int i) noexcept {
        return m_data.shapeKeys[i].deltas;
    }

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
