// GLBLoader.h
#pragma once

#include <cstdint>
#include <cstdio>


#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#include "string.hpp"
#include "vector.hpp"
#include "array.hpp"
#include "matrix.hpp"
#include "list.hpp"
#include "avltree.hpp"
#include "colordata.h"

// =================================================================================================

class GLBLoader {
public:
    struct ShapeKeySet {
        String name;
        AutoArray<Vector3f> deltas; // triangle soup: same length as vertices
        AutoArray<Vector3f> normalDeltas; // triangle soup: same length as vertices
    };

    struct MeshData {
        AutoArray<Vector3f>         vertices;   // triangle soup: 3 * triCount
        AutoArray<RGBAColor>        colors;     // 3 * triCount
        AutoArray<Vector3f>         normals;    // 1 * triCount
        List<ShapeKeySet>           shapeKeys;  // N sets, each has 3 * triCount deltas
    };

public:
    bool Load(const String& filename, bool fixModel = false);

    inline MeshData& Data() { 
        return m_data; 
    }

    inline const MeshData& Data() const { 
        return m_data; 
    }

private:
    struct PrimitiveData {
        AutoArray<Vector3f>             baseVertices;
        AutoArray<Vector3f>             baseNormals;
        AutoArray<uint32_t>             indices;
        AutoArray<AutoArray<Vector3f>>  morphVertices; // [target][vertex]
        AutoArray<AutoArray<Vector3f>>  morphNormals;  // [target][vertex]
        RGBAColor                       baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        int32_t                         targetCount{ 0 };
        int32_t                         triCount{ 0 };
		bool    			            haveNormals{ false };   
        bool                            isHull{ false };
    };

private:
    static Matrix4f NodeLocalMatrix(const tinygltf::Node& node);

    static bool ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, AutoArray<Vector3f>& out);

    static bool ReadAccessorIndicesU32(const tinygltf::Model& model, int accessorIndex, AutoArray<uint32_t>& out);

    static Vector4f PrimitiveBaseColor(const tinygltf::Model& model, int materialIndex);

    void CheckShapeKeyCount(int32_t targetCount);

    bool AppendFromNode(int nodeIndex, Matrix4f parentM);

    bool AppendMesh(int meshIndex, Matrix4f worldM);

    static int CompareVertices(void* context, const Vector3f& v1, const Vector3f& v2);

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

    inline AutoArray<Vector3f>& GetNormalShapeKeys(int i) noexcept {
        return m_data.shapeKeys[i].normalDeltas;
    }

    void Reset(void);

    ~GLBLoader() {
		Reset();
    }

private:
    tinygltf::Model             m_model;
    MeshData                    m_data;
    AutoArray<uint8_t>          m_isHullVertex;
    AVLTree<Vector3f, int32_t>  m_hullVertexMap;
    bool                        m_fixModel{ false };


    bool AppendPrimitive(tinygltf::Primitive& prim, Matrix4f worldM);

    bool ValidateTriangles(tinygltf::Primitive& prim);

    bool LoadVertices(tinygltf::Primitive& prim, PrimitiveData& in);

    bool LoadIndices(tinygltf::Primitive& prim, PrimitiveData& in);

    void WeldVertices(PrimitiveData& in);

    bool LoadNormals(tinygltf::Primitive& prim, PrimitiveData& in);

    bool LoadMorphTargets(tinygltf::Primitive& prim, PrimitiveData& in);

    void CorrectMorphTargets(PrimitiveData& in);

    bool ComputeNormals(const AutoArray<Vector3f>& vertices, const AutoArray<uint32_t>& indices, AutoArray<Vector3f>& normals);

    bool ComputeMorphNormals(PrimitiveData& in);

    void ReserveOutput(const PrimitiveData& in);

    void BuildShapeKeyPointers(AutoArray<ShapeKeySet*>& keyPtrs);

    bool AppendTriangles(PrimitiveData& in, Matrix4f worldM, AutoArray<ShapeKeySet*>& keyPtrs);

    void RecomputeMorphDeltas(ShapeKeySet& sk, const AutoArray<Vector3f>& morphedVertices);

    void StitchPrimitives(void);

    bool SaveToFile(const String& filename) const;

    bool LoadFromFile(const String& filename);
};

// =================================================================================================
