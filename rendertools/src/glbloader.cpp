// GLBLoader.cpp
#include "GLBLoader.h"

#include <algorithm>
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

// =================================================================================================

bool GLBLoader::Load(const String& filename) {
    m_data.vertices.Clear();
    m_data.colors.Clear();
    m_data.normals.Clear();
    m_data.shapeKeys.Clear();

    tinygltf::TinyGLTF loader;
    std::string errorMsg;
    std::string warningMsg;

    std::string fn = filename;

    if (not loader.LoadBinaryFromFile(&m_model, &errorMsg, &warningMsg, fn)) {
        fprintf(stderr, "GLBLoader: LoadBinaryFromFile failed: %s\n", errorMsg.c_str());
        return false;
    }

    if (m_model.scenes.empty()) {
        fprintf(stderr, "GLBLoader: no scenes found\n");
        return false;
    }

    int sceneIndex = m_model.defaultScene;
    if (sceneIndex < 0 or sceneIndex >= static_cast<int>(m_model.scenes.size())) {
        sceneIndex = 0;
    }

    auto& scene = m_model.scenes[static_cast<size_t>(sceneIndex)];

    Matrix4f identity;

    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        int nodeIndex = scene.nodes[i];
        if (not AppendFromNode(nodeIndex, identity)) {
            return false;
        }
    }

    return true;
}

// =================================================================================================

void GLBLoader::CheckShapeKeyCount(int32_t targetCount) {
    int32_t keyCount = m_data.shapeKeys.Length();
    if (keyCount >= targetCount) {
        return;
    }

    int32_t existingVertexCount = m_data.vertices.Length();

    for (int32_t i = keyCount; i < targetCount; ++i) {
        ShapeKeySet sk;
        sk.name = String("shapeKey") + String(i);
        sk.deltas.Resize(existingVertexCount, Vector3f(0.0f, 0.0f, 0.0f));
        m_data.shapeKeys.Append(std::move(sk));
    }
}

// =================================================================================================

bool GLBLoader::AppendFromNode(int nodeIndex, Matrix4f parentM) {
    if (nodeIndex < 0 or nodeIndex >= static_cast<int>(m_model.nodes.size())) {
        fprintf(stderr, "GLBLoader: node index out of range\n");
        return false;
    }

    auto& node = m_model.nodes[static_cast<size_t>(nodeIndex)];

    Matrix4f localM = NodeLocalMatrix(node);
    Matrix4f worldM = parentM * localM;

    if (node.mesh >= 0) {
        if (not AppendMesh(node.mesh, worldM)) {
            return false;
        }
    }

    for (size_t i = 0; i < node.children.size(); ++i) {
        int childIndex = node.children[i];
        if (not AppendFromNode(childIndex, worldM)) {
            return false;
        }
    }

    return true;
}

// =================================================================================================

bool GLBLoader::AppendMesh(int meshIndex, Matrix4f worldM) {
    if (meshIndex < 0 or meshIndex >= static_cast<int>(m_model.meshes.size())) {
        fprintf(stderr, "GLBLoader: mesh index out of range\n");
        return false;
    }

    auto& mesh = m_model.meshes[static_cast<size_t>(meshIndex)];

    for (size_t p = 0; p < mesh.primitives.size(); ++p) {
        auto& prim = mesh.primitives[p];
        if (not AppendPrimitive(prim, worldM)) {
            return false;
        }
    }

    return true;
}

// =================================================================================================

bool GLBLoader::AppendPrimitive(tinygltf::Primitive& prim, Matrix4f worldM) {
    if (not ValidateTriangles(prim)) {
        return false;
    }

    PrimitiveInput in;

    if (not LoadPositions(prim, in)) {
        return false;
    }

    if (not LoadMorphTargets(prim, in)) {
        return false;
    }

    in.baseColor = PrimitiveBaseColor(m_model, prim.material);

    static Conversions::FloatInterval placeholderColor{ 0.99f, 0.992f };
    if (placeholderColor.Contains(in.baseColor.R()) and placeholderColor.Contains(in.baseColor.G()) and placeholderColor.Contains(in.baseColor.B()))
        in.baseColor = RGBAColor(0.0f, 0.0f, 0.0f, -1.0f);

    if (not LoadIndices(prim, in)) {
        return false;
    }

    ReserveOutput(in);

    AutoArray<ShapeKeySet*> keyPtrs;
    BuildShapeKeyPointers(keyPtrs);

    if (not AppendTriangles(in, worldM, keyPtrs)) {
        return false;
    }

    return true;
}

bool GLBLoader::ValidateTriangles(tinygltf::Primitive& prim) {
    int mode = prim.mode;
    if (mode == -1) {
        mode = 4;
    }

    if (mode != 4) {
        fprintf(stderr, "GLBLoader: primitive mode is not TRIANGLES\n");
        return false;
    }

    return true;
}

bool GLBLoader::LoadPositions(tinygltf::Primitive& prim, PrimitiveInput& in) {
    auto itPos = prim.attributes.find("POSITION");
    if (itPos == prim.attributes.end()) {
        fprintf(stderr, "GLBLoader: primitive POSITION missing\n");
        return false;
    }

    if (not ReadAccessorVec3Float(m_model, itPos->second, in.basePos)) {
        return false;
    }

    if (in.basePos.IsEmpty()) {
        fprintf(stderr, "GLBLoader: primitive POSITION empty\n");
        return false;
    }

    return true;
}

bool GLBLoader::LoadMorphTargets(tinygltf::Primitive& prim, PrimitiveInput& in) {
    in.targetCount = static_cast<int32_t>(prim.targets.size());
    CheckShapeKeyCount(in.targetCount);

    in.morphPos.Resize(in.targetCount);

    for (int32_t t = 0; t < in.targetCount; ++t) {
        in.morphPos[t].Resize(in.basePos.Length(), Vector3f(0.0f, 0.0f, 0.0f));

        auto& target = prim.targets[static_cast<size_t>(t)];
        auto it = target.find("POSITION");
        if (it != target.end()) {
            if (not ReadAccessorVec3Float(m_model, it->second, in.morphPos[t])) {
                return false;
            }
        }
    }

    return true;
}

bool GLBLoader::LoadIndices(tinygltf::Primitive& prim, PrimitiveInput& in) {
    in.indices.Clear();

    if (prim.indices >= 0) {
        if (not ReadAccessorIndicesU32(m_model, prim.indices, in.indices)) {
            return false;
        }
    }
    else {
        in.indices.Resize(in.basePos.Length());
        for (int32_t i = 0; i < in.indices.Length(); ++i) {
            in.indices[i] = static_cast<uint32_t>(i);
        }
    }

    if ((in.indices.Length() % 3) != 0) {
        fprintf(stderr, "GLBLoader: index count not divisible by 3\n");
        return false;
    }

    in.triCount = in.indices.Length() / 3;

    return true;
}

void GLBLoader::ReserveOutput(const PrimitiveInput& in) {
    int32_t addVertexCount = in.triCount * 3;

    m_data.vertices.Reserve(m_data.vertices.Length() + addVertexCount);
    m_data.colors.Reserve(m_data.colors.Length() + addVertexCount);
    m_data.normals.Reserve(m_data.normals.Length() + in.triCount);

    for (auto& sk : m_data.shapeKeys) {
        sk.deltas.Reserve(sk.deltas.Length() + addVertexCount);
    }
}

void GLBLoader::BuildShapeKeyPointers(AutoArray<ShapeKeySet*>& keyPtrs) {
    int32_t globalKeyCount = m_data.shapeKeys.Length();
    keyPtrs.Resize(globalKeyCount);

    int32_t k = 0;
    for (auto& sk : m_data.shapeKeys) {
        keyPtrs[k] = &sk;
        k += 1;
    }
}

bool GLBLoader::AppendTriangles(const PrimitiveInput& in, Matrix4f worldM, AutoArray<ShapeKeySet*>& keyPtrs) {
    int32_t globalKeyCount = keyPtrs.Length();

    for (int32_t t = 0; t < in.triCount; ++t) {
        uint32_t i0 = in.indices[t * 3 + 0];
        uint32_t i1 = in.indices[t * 3 + 1];
        uint32_t i2 = in.indices[t * 3 + 2];

        if (i0 >= static_cast<uint32_t>(in.basePos.Length()) or
            i1 >= static_cast<uint32_t>(in.basePos.Length()) or
            i2 >= static_cast<uint32_t>(in.basePos.Length())) {
            fprintf(stderr, "GLBLoader: index out of range\n");
            return false;
        }

        Vector3f p0 = TransformPosition(worldM, in.basePos[static_cast<int32_t>(i0)]);
        Vector3f p1 = TransformPosition(worldM, in.basePos[static_cast<int32_t>(i1)]);
        Vector3f p2 = TransformPosition(worldM, in.basePos[static_cast<int32_t>(i2)]);

        Vector3f n = Vector3f::Normal(p0, p1, p2);
        m_data.normals.Append(n);

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

// =================================================================================================

Matrix4f GLBLoader::NodeLocalMatrix(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        float data[16];
        for (int i = 0; i < 16; ++i) {
            data[i] = static_cast<float>(node.matrix[static_cast<size_t>(i)]);
        }
        return Matrix4f(data);
    }

    glm::vec3 t(0.0f, 0.0f, 0.0f);
    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 s(1.0f, 1.0f, 1.0f);

    if (node.translation.size() == 3) {
        t.x = static_cast<float>(node.translation[0]);
        t.y = static_cast<float>(node.translation[1]);
        t.z = static_cast<float>(node.translation[2]);
    }

    if (node.rotation.size() == 4) {
        r.x = static_cast<float>(node.rotation[0]);
        r.y = static_cast<float>(node.rotation[1]);
        r.z = static_cast<float>(node.rotation[2]);
        r.w = static_cast<float>(node.rotation[3]);
    }

    if (node.scale.size() == 3) {
        s.x = static_cast<float>(node.scale[0]);
        s.y = static_cast<float>(node.scale[1]);
        s.z = static_cast<float>(node.scale[2]);
    }

    glm::mat4 tm = glm::translate(glm::mat4(1.0f), t);
    glm::mat4 rm = glm::mat4_cast(r);
    glm::mat4 sm = glm::scale(glm::mat4(1.0f), s);

    return Matrix4f(tm * rm * sm);
}

// =================================================================================================

bool GLBLoader::ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, AutoArray<Vector3f>& out) {
    if (accessorIndex < 0 or accessorIndex >= static_cast<int>(model.accessors.size())) {
        fprintf(stderr, "GLBLoader: accessor index out of range\n");
        return false;
    }

    auto& acc = model.accessors[static_cast<size_t>(accessorIndex)];

    if (acc.sparse.isSparse) {
        fprintf(stderr, "GLBLoader: sparse accessors not supported\n");
        return false;
    }

    if (acc.type != TINYGLTF_TYPE_VEC3) {
        fprintf(stderr, "GLBLoader: accessor is not VEC3\n");
        return false;
    }

    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        fprintf(stderr, "GLBLoader: accessor componentType is not FLOAT\n");
        return false;
    }

    if (acc.bufferView < 0 or acc.bufferView >= static_cast<int>(model.bufferViews.size())) {
        fprintf(stderr, "GLBLoader: bufferView index out of range\n");
        return false;
    }

    auto& view = model.bufferViews[static_cast<size_t>(acc.bufferView)];

    if (view.buffer < 0 or view.buffer >= static_cast<int>(model.buffers.size())) {
        fprintf(stderr, "GLBLoader: buffer index out of range\n");
        return false;
    }

    auto& buf = model.buffers[static_cast<size_t>(view.buffer)];

    size_t stride = static_cast<size_t>(view.byteStride);
    if (stride == 0) {
        stride = sizeof(float) * 3;
    }

    if (stride < sizeof(float) * 3) {
        fprintf(stderr, "GLBLoader: invalid stride for VEC3\n");
        return false;
    }

    size_t base = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(acc.byteOffset);
    size_t need = base + stride * static_cast<size_t>(acc.count);

    if (need > buf.data.size()) {
        fprintf(stderr, "GLBLoader: buffer overrun in ReadAccessorVec3Float\n");
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

bool GLBLoader::ReadAccessorIndicesU32(const tinygltf::Model& model, int accessorIndex, AutoArray<uint32_t>& out) {
    if (accessorIndex < 0 or accessorIndex >= static_cast<int>(model.accessors.size())) {
        fprintf(stderr, "GLBLoader: accessor index out of range\n");
        return false;
    }

    auto& acc = model.accessors[static_cast<size_t>(accessorIndex)];

    if (acc.sparse.isSparse) {
        fprintf(stderr, "GLBLoader: sparse accessors not supported\n");
        return false;
    }

    if (acc.type != TINYGLTF_TYPE_SCALAR) {
        fprintf(stderr, "GLBLoader: indices accessor is not SCALAR\n");
        return false;
    }

    if (acc.bufferView < 0 or acc.bufferView >= static_cast<int>(model.bufferViews.size())) {
        fprintf(stderr, "GLBLoader: bufferView index out of range\n");
        return false;
    }

    auto& view = model.bufferViews[static_cast<size_t>(acc.bufferView)];

    if (view.buffer < 0 or view.buffer >= static_cast<int>(model.buffers.size())) {
        fprintf(stderr, "GLBLoader: buffer index out of range\n");
        return false;
    }

    auto& buf = model.buffers[static_cast<size_t>(view.buffer)];

    size_t elemSize = 0;
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        elemSize = 1;
    }
    else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        elemSize = 2;
    }
    else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        elemSize = 4;
    }
    else {
        fprintf(stderr, "GLBLoader: unsupported index componentType\n");
        return false;
    }

    size_t stride = static_cast<size_t>(view.byteStride);
    if (stride == 0) {
        stride = elemSize;
    }

    if (stride < elemSize) {
        fprintf(stderr, "GLBLoader: invalid stride for indices\n");
        return false;
    }

    size_t base = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(acc.byteOffset);
    size_t need = base + stride * static_cast<size_t>(acc.count);

    if (need > buf.data.size()) {
        fprintf(stderr, "GLBLoader: buffer overrun in ReadAccessorIndicesU32\n");
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

Vector4f GLBLoader::PrimitiveBaseColor(const tinygltf::Model& model, int materialIndex) {
    if (materialIndex < 0 or materialIndex >= static_cast<int>(model.materials.size())) {
        return Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
    }

    auto& mat = model.materials[static_cast<size_t>(materialIndex)];
    auto& f = mat.pbrMetallicRoughness.baseColorFactor;

    if (f.size() == 4) {
        return Vector4f(
            static_cast<float>(f[0]),
            static_cast<float>(f[1]),
            static_cast<float>(f[2]),
            static_cast<float>(f[3])
        );
    }

    return Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
}

// =================================================================================================