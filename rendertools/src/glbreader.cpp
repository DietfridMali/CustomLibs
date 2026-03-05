// GLBReader.cpp
#include "GLBReader.h"

#include <cstring>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "conversions.hpp"

// =================================================================================================

static Vector3f TransformPosition(Matrix4f m, Vector3f p) {
    Vector4f h(p.x, p.y, p.z, 1.0f);
    Vector4f r = m * h;
    return Vector3f(r.x, r.y, r.z);
}

static Vector3f TransformDelta(Matrix4f m, Vector3f d) {
    glm::mat3 a = glm::mat3(m.m);
    glm::vec3 r = a * glm::vec3(d.x, d.y, d.z);
    return Vector3f(r.x, r.y, r.z);
}

bool GLBReader::AppendMesh(int meshIndex, Matrix4f worldM) {
    if (meshIndex < 0 or meshIndex >= static_cast<int>(m_model.meshes.size())) {
        fprintf(stderr, "GLBReader: mesh index out of range\n");
        return false;
    }

    auto& mesh = m_model.meshes[static_cast<size_t>(meshIndex)];

    for (size_t p = 0; p < mesh.primitives.size(); ++p) {
        auto& prim = mesh.primitives[p];

        if (not AppendPrimitive(prim, worldM))
            return false;
    }
    return true;
}

bool GLBReader::AppendPrimitive(tinygltf::Primitive& prim, Matrix4f worldM) {
    if (not ValidateTriangles(prim))
        return false;

    PrimitiveInput in;

    if (not LoadPositions(prim, in))
        return false;

    if (not LoadMorphTargets(prim, in))
        return false;

    in.baseColor = PrimitiveBaseColor(m_model, prim.material);

    if (not LoadIndices(prim, in))
        return false;

    ReserveOutput(in);

    AutoArray<ShapeKeySet*> keyPtrs;
    BuildShapeKeyPointers(keyPtrs);

    if (not AppendTriangles(in, worldM, keyPtrs))
        return false;

    return true;
}

bool GLBReader::ValidateTriangles(tinygltf::Primitive& prim) {
    int mode = prim.mode;

    if (mode == -1)
        mode = 4;

    if (mode != 4) {
        fprintf(stderr, "GLBReader: primitive mode is not TRIANGLES\n");
        return false;
    }
    return true;
}

bool GLBReader::LoadPositions(tinygltf::Primitive& prim, PrimitiveInput& in) {
    auto itPos = prim.attributes.find("POSITION");

    if (itPos == prim.attributes.end()) {
        fprintf(stderr, "GLBReader: primitive POSITION missing\n");
        return false;
    }

    if (not ReadAccessorVec3Float(m_model, itPos->second, in.basePos))
        return false;

    if (in.basePos.IsEmpty()) {
        fprintf(stderr, "GLBReader: primitive POSITION empty\n");
        return false;
    }

    return true;
}

bool GLBReader::LoadMorphTargets(tinygltf::Primitive& prim, PrimitiveInput& in) {
    in.targetCount = static_cast<int32_t>(prim.targets.size());

    CheckShapeKeyCount(in.targetCount);

    in.morphPos.Resize(in.targetCount);

    for (int32_t t = 0; t < in.targetCount; ++t) {
        in.morphPos[t].Resize(in.basePos.Length(), Vector3f(0.0f, 0.0f, 0.0f));

        auto it = prim.targets[static_cast<size_t>(t)].find("POSITION");
        if (it != prim.targets[static_cast<size_t>(t)].end()) {
            if (not ReadAccessorVec3Float(m_model, it->second, in.morphPos[t]))
                return false;
        }
    }

    return true;
}

bool GLBReader::LoadIndices(tinygltf::Primitive& prim, PrimitiveInput& in) {
    in.indices.Clear();

    if (prim.indices >= 0) {
        if (not ReadAccessorIndicesU32(m_model, prim.indices, in.indices))
            return false;
    }
    else {
        in.indices.Resize(in.basePos.Length());
        for (int32_t i = 0; i < in.indices.Length(); ++i)
            in.indices[i] = static_cast<uint32_t>(i);
    }

    if ((in.indices.Length() % 3) != 0) {
        fprintf(stderr, "GLBReader: index count not divisible by 3\n");
        return false;
    }

    in.triCount = in.indices.Length() / 3;

    return true;
}

void GLBReader::ReserveOutput(const PrimitiveInput& in) {
    int32_t addVertexCount = in.triCount * 3;

    m_data.vertices.Reserve(m_data.vertices.Length() + addVertexCount);
    m_data.colors.Reserve(m_data.colors.Length() + addVertexCount);
    m_data.triNormals.Reserve(m_data.triNormals.Length() + in.triCount);

    for (auto& sk : m_data.shapeKeys)
        sk.deltas.Reserve(sk.deltas.Length() + addVertexCount);
}

void GLBReader::BuildShapeKeyPointers(AutoArray<ShapeKeySet*>& keyPtrs) {
    int32_t globalKeyCount = m_data.shapeKeys.Length();

    keyPtrs.Resize(globalKeyCount);

    int32_t k = 0;
    for (auto& sk : m_data.shapeKeys) {
        keyPtrs[k] = &sk;
        k += 1;
    }
}

bool GLBReader::AppendTriangles(const PrimitiveInput& in, Matrix4f worldM, AutoArray<ShapeKeySet*>& keyPtrs) {
    int32_t globalKeyCount = keyPtrs.Length();

    for (int32_t t = 0; t < in.triCount; ++t) {
        uint32_t i0 = in.indices[t * 3 + 0];
        uint32_t i1 = in.indices[t * 3 + 1];
        uint32_t i2 = in.indices[t * 3 + 2];

        if (i0 >= static_cast<uint32_t>(in.basePos.Length()) or
            i1 >= static_cast<uint32_t>(in.basePos.Length()) or
            i2 >= static_cast<uint32_t>(in.basePos.Length())) {
            fprintf(stderr, "GLBReader: index out of range\n");
            return false;
        }

        Vector3f p0 = TransformPosition(worldM, in.basePos[static_cast<int32_t>(i0)]);
        Vector3f p1 = TransformPosition(worldM, in.basePos[static_cast<int32_t>(i1)]);
        Vector3f p2 = TransformPosition(worldM, in.basePos[static_cast<int32_t>(i2)]);

        Vector3f n = Vector3f::Normal(p0, p1, p2);

        m_data.triNormals.Append(n);

        m_data.vertices.Append(p0);
        m_data.vertices.Append(p1);
        m_data.vertices.Append(p2);

        m_data.colors.Append(in.baseColor);
        m_data.colors.Append(in.baseColor);
        m_data.colors.Append(in.baseColor);

        for (int32_t k = 0; k < globalKeyCount; ++k) {
            Vector3f d0(0.0f, 0.0f, 0.0f);
            Vector3f d1(0.0f, 0.0f, 0.0f);
            Vector3f d2(0.0f, 0.0f, 0.0f);

            if (k < in.targetCount) {
                d0 = TransformDelta(worldM, in.morphPos[k][static_cast<int32_t>(i0)]);
                d1 = TransformDelta(worldM, in.morphPos[k][static_cast<int32_t>(i1)]);
                d2 = TransformDelta(worldM, in.morphPos[k][static_cast<int32_t>(i2)]);
            }

            keyPtrs[k]->deltas.Append(d0);
            keyPtrs[k]->deltas.Append(d1);
            keyPtrs[k]->deltas.Append(d2);
        }
    }

    return true;
}

bool GLBReader::ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, AutoArray<Vector3f>& out) {
    if (accessorIndex < 0 or accessorIndex >= static_cast<int>(model.accessors.size())) {
        fprintf(stderr, "GLBReader: accessor index out of range\n");
        return false;
    }

    auto& acc = model.accessors[static_cast<size_t>(accessorIndex)];

    if (acc.sparse.isSparse) {
        fprintf(stderr, "GLBReader: sparse accessors not supported\n");
        return false;
    }
    if (acc.type != TINYGLTF_TYPE_VEC3) {
        fprintf(stderr, "GLBReader: accessor is not VEC3\n");
        return false;
    }
    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        fprintf(stderr, "GLBReader: accessor componentType is not FLOAT\n");
        return false;
    }
    if (acc.bufferView < 0 or acc.bufferView >= static_cast<int>(model.bufferViews.size())) {
        fprintf(stderr, "GLBReader: bufferView index out of range\n");
        return false;
    }

    auto& view = model.bufferViews[static_cast<size_t>(acc.bufferView)];
    if (view.buffer < 0 or view.buffer >= static_cast<int>(model.buffers.size())) {
        fprintf(stderr, "GLBReader: buffer index out of range\n");
        return false;
    }

    auto& buf = model.buffers[static_cast<size_t>(view.buffer)];

    size_t stride = static_cast<size_t>(view.byteStride);
    if (stride == 0)
        stride = sizeof(float) * 3;

    if (stride < sizeof(float) * 3) {
        fprintf(stderr, "GLBReader: invalid stride for VEC3\n");
        return false;
    }

    size_t base = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(acc.byteOffset);
    size_t need = base + stride * static_cast<size_t>(acc.count);

    if (need > buf.data.size()) {
        fprintf(stderr, "GLBReader: buffer overrun in ReadAccessorVec3Float\n");
        return false;
    }

    out.Resize(static_cast<int32_t>(acc.count));

    for (size_t i = 0; i < static_cast<size_t>(acc.count); ++i) {
        size_t off = base + i * stride;

        float fx;
        float fy;
        float fz;

        std::memcpy(&fx, buf.data.data() + off + 0, sizeof(float));
        std::memcpy(&fy, buf.data.data() + off + 4, sizeof(float));
        std::memcpy(&fz, buf.data.data() + off + 8, sizeof(float));

        out[static_cast<int32_t>(i)] = Vector3f(fx, fy, fz);
    }

    return true;
}

bool GLBReader::ReadAccessorIndicesU32(const tinygltf::Model& model, int accessorIndex, AutoArray<uint32_t>& out) {
    if (accessorIndex < 0 or accessorIndex >= static_cast<int>(model.accessors.size())) {
        fprintf(stderr, "GLBReader: accessor index out of range\n");
        return false;
    }

    auto& acc = model.accessors[static_cast<size_t>(accessorIndex)];

    if (acc.sparse.isSparse) {
        fprintf(stderr, "GLBReader: sparse accessors not supported\n");
        return false;
    }
    if (acc.type != TINYGLTF_TYPE_SCALAR) {
        fprintf(stderr, "GLBReader: indices accessor is not SCALAR\n");
        return false;
    }
    if (acc.bufferView < 0 or acc.bufferView >= static_cast<int>(model.bufferViews.size())) {
        fprintf(stderr, "GLBReader: bufferView index out of range\n");
        return false;
    }

    auto& view = model.bufferViews[static_cast<size_t>(acc.bufferView)];
    if (view.buffer < 0 or view.buffer >= static_cast<int>(model.buffers.size())) {
        fprintf(stderr, "GLBReader: buffer index out of range\n");
        return false;
    }

    auto& buf = model.buffers[static_cast<size_t>(view.buffer)];

    size_t elemSize = 0;

    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
        elemSize = 1;
    else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
        elemSize = 2;
    else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
        elemSize = 4;
    else {
        fprintf(stderr, "GLBReader: unsupported index componentType\n");
        return false;
    }

    size_t stride = static_cast<size_t>(view.byteStride);
    if (stride == 0)
        stride = elemSize;

    size_t base = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(acc.byteOffset);
    size_t need = base + stride * static_cast<size_t>(acc.count);

    if (need > buf.data.size()) {
        fprintf(stderr, "GLBReader: buffer overrun in ReadAccessorIndicesU32\n");
        return false;
    }

    out.Resize(static_cast<int32_t>(acc.count));

    for (size_t i = 0; i < static_cast<size_t>(acc.count); ++i) {
        size_t off = base + i * stride;

        if (elemSize == 1) {
            uint8_t v;
            std::memcpy(&v, buf.data.data() + off, 1);
            out[static_cast<int32_t>(i)] = static_cast<uint32_t>(v);
        }
        else if (elemSize == 2) {
            uint16_t v;
            std::memcpy(&v, buf.data.data() + off, 2);
            out[static_cast<int32_t>(i)] = static_cast<uint32_t>(v);
        }
        else {
            uint32_t v;
            std::memcpy(&v, buf.data.data() + off, 4);
            out[static_cast<int32_t>(i)] = v;
        }
    }

    return true;
}

// =================================================================================================
