// GLBReader.cpp
#include "GLBReader.h"

#include <cstring>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

bool GLBReader::Load(const std::string& filename) {
    m_data.vertices.Clear();
    m_data.colors.Clear();
    m_data.triNormals.Clear();
    m_data.shapeKeys.Clear();

    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ok = loader.LoadBinaryFromFile(&m_model, &err, &warn, filename);
    if (not ok) {
        throw std::runtime_error("GLBReader: LoadBinaryFromFile failed: " + err);
    }

    if (m_model.scenes.empty()) {
        throw std::runtime_error("GLBReader: no scenes");
    }

    int sceneIndex = m_model.defaultScene;
    if (sceneIndex < 0) {
        sceneIndex = 0;
    }

    tinygltf::Scene& scene = m_model.scenes[static_cast<size_t>(sceneIndex)];
    Matrix4f identity = Matrix4f::Identity();

    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        int nodeIndex = scene.nodes[i];
        AppendFromNode(nodeIndex, identity);
    }

    return true;
}

void GLBReader::EnsureShapeKeyCount(int32_t targetCount) {
    int32_t have = m_data.shapeKeys.Length();
    if (have >= targetCount) {
        return;
    }

    int32_t existingVertexCount = m_data.vertices.Length();

    for (int32_t i = have; i < targetCount; ++i) {
        ShapeKeySet sk;
        sk.name = "shapeKey" + std::to_string(i);
        sk.deltas.Resize(existingVertexCount, Vector3f(0.0f, 0.0f, 0.0f));
        m_data.shapeKeys.Append(std::move(sk));
    }
}

void GLBReader::AppendFromNode(int nodeIndex, Matrix4f parentM) {
    if (nodeIndex < 0 or nodeIndex >= static_cast<int>(m_model.nodes.size())) {
        throw std::runtime_error("GLBReader: node index out of range");
    }

    tinygltf::Node& node = m_model.nodes[static_cast<size_t>(nodeIndex)];

    Matrix4f localM = NodeLocalMatrix(node);
    Matrix4f worldM = parentM * localM;

    if (node.mesh >= 0) {
        AppendMesh(node.mesh, worldM);
    }

    for (size_t i = 0; i < node.children.size(); ++i) {
        int childIndex = node.children[i];
        AppendFromNode(childIndex, worldM);
    }
}

void GLBReader::AppendMesh(int meshIndex, Matrix4f worldM) {
    if (meshIndex < 0 or meshIndex >= static_cast<int>(m_model.meshes.size())) {
        throw std::runtime_error("GLBReader: mesh index out of range");
    }

    tinygltf::Mesh& mesh = m_model.meshes[static_cast<size_t>(meshIndex)];

    for (size_t p = 0; p < mesh.primitives.size(); ++p) {
        tinygltf::Primitive& prim = mesh.primitives[p];

        int mode = prim.mode;
        if (mode == -1) {
            mode = 4;
        }
        if (mode != 4) {
            throw std::runtime_error("GLBReader: primitive mode is not TRIANGLES");
        }

        auto itPos = prim.attributes.find("POSITION");
        if (itPos == prim.attributes.end()) {
            throw std::runtime_error("GLBReader: primitive has no POSITION");
        }

        std::vector<Vector3f> srcPos;
        ReadAccessorVec3Float(m_model, itPos->second, srcPos);

        int32_t targetCount = static_cast<int32_t>(prim.targets.size());
        EnsureShapeKeyCount(targetCount);

        std::vector<std::vector<Vector3f>> srcMorph;
        srcMorph.resize(static_cast<size_t>(targetCount));

        for (int32_t t = 0; t < targetCount; ++t) {
            srcMorph[static_cast<size_t>(t)].resize(srcPos.size(), Vector3f(0.0f, 0.0f, 0.0f));

            auto it = prim.targets[static_cast<size_t>(t)].find("POSITION");
            if (it != prim.targets[static_cast<size_t>(t)].end()) {
                ReadAccessorVec3Float(m_model, it->second, srcMorph[static_cast<size_t>(t)]);
            }
        }

        Vector4f baseColor = PrimitiveBaseColor(m_model, prim.material);

        std::vector<uint32_t> idx;
        if (prim.indices >= 0) {
            ReadAccessorIndicesU32(m_model, prim.indices, idx);
        }
        else {
            idx.resize(srcPos.size());
            for (size_t i = 0; i < idx.size(); ++i) {
                idx[i] = static_cast<uint32_t>(i);
            }
        }

        if ((idx.size() % 3) != 0) {
            throw std::runtime_error("GLBReader: index count not divisible by 3");
        }

        size_t triCount = idx.size() / 3;

        int32_t globalKeyCount = m_data.shapeKeys.Length();
        int32_t addVertexCount = static_cast<int32_t>(triCount * 3);

        m_data.vertices.Reserve(m_data.vertices.Length() + addVertexCount);
        m_data.colors.Reserve(m_data.colors.Length() + addVertexCount);
        m_data.triNormals.Reserve(m_data.triNormals.Length() + static_cast<int32_t>(triCount));

        for (auto& sk : m_data.shapeKeys) {
            sk.deltas.Reserve(sk.deltas.Length() + addVertexCount);
        }

        std::vector<ShapeKeySet*> keyPtrs;
        keyPtrs.resize(static_cast<size_t>(globalKeyCount));
        {
            int32_t k = 0;
            for (auto& sk : m_data.shapeKeys) {
                keyPtrs[static_cast<size_t>(k)] = &sk;
                k += 1;
            }
        }

        for (size_t t = 0; t < triCount; ++t) {
            uint32_t i0 = idx[t * 3 + 0];
            uint32_t i1 = idx[t * 3 + 1];
            uint32_t i2 = idx[t * 3 + 2];

            if (i0 >= srcPos.size() or i1 >= srcPos.size() or i2 >= srcPos.size()) {
                throw std::runtime_error("GLBReader: index out of range");
            }

            Vector3f p0 = TransformPosition(worldM, srcPos[i0]);
            Vector3f p1 = TransformPosition(worldM, srcPos[i1]);
            Vector3f p2 = TransformPosition(worldM, srcPos[i2]);

            Vector3f n = Vector3f::Normal(p0, p1, p2);

            m_data.triNormals.Append(n);

            m_data.vertices.Append(p0);
            m_data.vertices.Append(p1);
            m_data.vertices.Append(p2);

            m_data.colors.Append(baseColor);
            m_data.colors.Append(baseColor);
            m_data.colors.Append(baseColor);

            for (int32_t k = 0; k < globalKeyCount; ++k) {
                Vector3f d0(0.0f, 0.0f, 0.0f);
                Vector3f d1(0.0f, 0.0f, 0.0f);
                Vector3f d2(0.0f, 0.0f, 0.0f);

                if (k < targetCount) {
                    d0 = TransformDelta(worldM, srcMorph[static_cast<size_t>(k)][i0]);
                    d1 = TransformDelta(worldM, srcMorph[static_cast<size_t>(k)][i1]);
                    d2 = TransformDelta(worldM, srcMorph[static_cast<size_t>(k)][i2]);
                }

                keyPtrs[static_cast<size_t>(k)]->deltas.Append(d0);
                keyPtrs[static_cast<size_t>(k)]->deltas.Append(d1);
                keyPtrs[static_cast<size_t>(k)]->deltas.Append(d2);
            }
        }
    }
}

Matrix4f GLBReader::NodeLocalMatrix(const tinygltf::Node& node) {
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

void GLBReader::ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, std::vector<Vector3f>& out) {
    if (accessorIndex < 0 or accessorIndex >= static_cast<int>(model.accessors.size())) {
        throw std::runtime_error("GLBReader: accessor index out of range");
    }

    tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(accessorIndex)];

    if (acc.sparse.isSparse) {
        throw std::runtime_error("GLBReader: sparse accessors not supported here");
    }
    if (acc.type != TINYGLTF_TYPE_VEC3) {
        throw std::runtime_error("GLBReader: accessor is not VEC3");
    }
    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        throw std::runtime_error("GLBReader: accessor componentType is not FLOAT");
    }
    if (acc.bufferView < 0 or acc.bufferView >= static_cast<int>(model.bufferViews.size())) {
        throw std::runtime_error("GLBReader: bufferView index out of range");
    }

    tinygltf::BufferView& view = model.bufferViews[static_cast<size_t>(acc.bufferView)];
    if (view.buffer < 0 or view.buffer >= static_cast<int>(model.buffers.size())) {
        throw std::runtime_error("GLBReader: buffer index out of range");
    }

    tinygltf::Buffer& buf = model.buffers[static_cast<size_t>(view.buffer)];

    size_t stride = view.byteStride;
    if (stride == 0) {
        stride = sizeof(float) * 3;
    }

    size_t base = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(acc.byteOffset);
    size_t need = base + stride * static_cast<size_t>(acc.count);

    if (need > buf.data.size()) {
        throw std::runtime_error("GLBReader: buffer overrun in ReadAccessorVec3Float");
    }

    out.resize(acc.count);

    for (size_t i = 0; i < static_cast<size_t>(acc.count); ++i) {
        size_t off = base + i * stride;

        float fx;
        float fy;
        float fz;

        std::memcpy(&fx, buf.data.data() + off + 0, 4);
        std::memcpy(&fy, buf.data.data() + off + 4, 4);
        std::memcpy(&fz, buf.data.data() + off + 8, 4);

        out[i] = Vector3f(fx, fy, fz);
    }
}

void GLBReader::ReadAccessorIndicesU32(const tinygltf::Model& model, int accessorIndex, std::vector<uint32_t>& out) {
    if (accessorIndex < 0 or accessorIndex >= static_cast<int>(model.accessors.size())) {
        throw std::runtime_error("GLBReader: accessor index out of range");
    }

    tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(accessorIndex)];

    if (acc.sparse.isSparse) {
        throw std::runtime_error("GLBReader: sparse accessors not supported here");
    }
    if (acc.type != TINYGLTF_TYPE_SCALAR) {
        throw std::runtime_error("GLBReader: indices accessor is not SCALAR");
    }
    if (acc.bufferView < 0 or acc.bufferView >= static_cast<int>(model.bufferViews.size())) {
        throw std::runtime_error("GLBReader: bufferView index out of range");
    }

    tinygltf::BufferView& view = model.bufferViews[static_cast<size_t>(acc.bufferView)];
    if (view.buffer < 0 or view.buffer >= static_cast<int>(model.buffers.size())) {
        throw std::runtime_error("GLBReader: buffer index out of range");
    }

    tinygltf::Buffer& buf = model.buffers[static_cast<size_t>(view.buffer)];

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
        throw std::runtime_error("GLBReader: unsupported index componentType");
    }

    size_t stride = view.byteStride;
    if (stride == 0) {
        stride = elemSize;
    }

    size_t base = static_cast<size_t>(view.byteOffset) + static_cast<size_t>(acc.byteOffset);
    size_t need = base + stride * static_cast<size_t>(acc.count);

    if (need > buf.data.size()) {
        throw std::runtime_error("GLBReader: buffer overrun in ReadAccessorIndicesU32");
    }

    out.resize(acc.count);

    for (size_t i = 0; i < static_cast<size_t>(acc.count); ++i) {
        size_t off = base + i * stride;

        if (elemSize == 1) {
            uint8_t v;
            std::memcpy(&v, buf.data.data() + off, 1);
            out[i] = static_cast<uint32_t>(v);
        }
        else if (elemSize == 2) {
            uint16_t v;
            std::memcpy(&v, buf.data.data() + off, 2);
            out[i] = static_cast<uint32_t>(v);
        }
        else {
            uint32_t v;
            std::memcpy(&v, buf.data.data() + off, 4);
            out[i] = v;
        }
    }
}

Vector4f GLBReader::PrimitiveBaseColor(const tinygltf::Model& model, int materialIndex) {
    if (materialIndex < 0 or materialIndex >= static_cast<int>(model.materials.size())) {
        return Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
    }

    tinygltf::Material& mat = model.materials[static_cast<size_t>(materialIndex)];
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
