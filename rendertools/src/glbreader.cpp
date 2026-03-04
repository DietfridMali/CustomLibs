// GLBReader.cpp
#include "glbreader.h"

static Vector3f TransformPosition(glm::mat4 m, Vector3f p) {
    glm::vec4 v = glm::vec4(p.x, p.y, p.z, 1.0f);
    glm::vec4 r = m * v;
    return Vector3f(r.x, r.y, r.z);
}

static Vector3f TransformDelta(glm::mat4 m, Vector3f d) {
    glm::mat3 a = glm::mat3(m);
    glm::vec3 r = a * glm::vec3(d.x, d.y, d.z);
    return Vector3f(r.x, r.y, r.z);
}

bool GLBReader::Load(const std::string& filename) {
    m_data.vertices.Clear();
    m_data.colors.Clear();
    m_data.morph0.Clear();
    m_data.morph1.Clear();
    m_data.triNormals.Clear();

    tinygltf::TinyGLTF loader;

    std::string err;
    std::string warn;

    bool ok = loader.LoadBinaryFromFile(&m_model, &err, &warn, filename);

    if (not warn.empty()) {
        // optional: ignore
    }

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

    glm::mat4 identity = glm::mat4(1.0f);

    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        int nodeIndex = scene.nodes[i];
        AppendMeshFromNode(nodeIndex, identity);
    }

    return true;
}

void GLBReader::AppendMeshFromNode(int nodeIndex, glm::mat4 parentM) {
    if (nodeIndex < 0 or nodeIndex >= static_cast<int>(m_model.nodes.size())) {
        throw std::runtime_error("GLBReader: node index out of range");
    }

    tinygltf::Node& node = m_model.nodes[static_cast<size_t>(nodeIndex)];

    glm::mat4 localM = NodeLocalMatrix(node);
    glm::mat4 worldM = parentM * localM;

    if (node.mesh >= 0) {
        AppendMesh(node.mesh, worldM);
    }

    for (size_t i = 0; i < node.children.size(); ++i) {
        int childIndex = node.children[i];
        AppendMeshFromNode(childIndex, worldM);
    }
}

void GLBReader::AppendMesh(int meshIndex, glm::mat4 worldM) {
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

        std::vector<Vector3f> srcMorph0;
        std::vector<Vector3f> srcMorph1;

        srcMorph0.resize(srcPos.size(), Vector3f(0.0f, 0.0f, 0.0f));
        srcMorph1.resize(srcPos.size(), Vector3f(0.0f, 0.0f, 0.0f));

        if (prim.targets.size() >= 1) {
            auto itM0 = prim.targets[0].find("POSITION");
            if (itM0 != prim.targets[0].end()) {
                ReadAccessorVec3Float(m_model, itM0->second, srcMorph0);
            }
        }

        if (prim.targets.size() >= 2) {
            auto itM1 = prim.targets[1].find("POSITION");
            if (itM1 != prim.targets[1].end()) {
                ReadAccessorVec3Float(m_model, itM1->second, srcMorph1);
            }
        }

        Vector4f baseColor = PrimitiveBaseColor(m_model, prim.material);

        std::vector<uint32_t> idx;
        if (prim.indices >= 0) {
            ReadAccessorIndices(m_model, prim.indices, idx);
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

        m_data.vertices.Reserve(m_data.vertices.Length() + static_cast<int32_t>(triCount * 3));
        m_data.colors.Reserve(m_data.colors.Length() + static_cast<int32_t>(triCount * 3));
        m_data.morph0.Reserve(m_data.morph0.Length() + static_cast<int32_t>(triCount * 3));
        m_data.morph1.Reserve(m_data.morph1.Length() + static_cast<int32_t>(triCount * 3));
        m_data.triNormals.Reserve(m_data.triNormals.Length() + static_cast<int32_t>(triCount));

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

            Vector3f d00 = TransformDelta(worldM, srcMorph0[i0]);
            Vector3f d01 = TransformDelta(worldM, srcMorph0[i1]);
            Vector3f d02 = TransformDelta(worldM, srcMorph0[i2]);

            Vector3f d10 = TransformDelta(worldM, srcMorph1[i0]);
            Vector3f d11 = TransformDelta(worldM, srcMorph1[i1]);
            Vector3f d12 = TransformDelta(worldM, srcMorph1[i2]);

            m_data.morph0.Append(d00);
            m_data.morph0.Append(d01);
            m_data.morph0.Append(d02);

            m_data.morph1.Append(d10);
            m_data.morph1.Append(d11);
            m_data.morph1.Append(d12);
        }
    }
}

glm::mat4 GLBReader::NodeLocalMatrix(const tinygltf::Node& node) {
    glm::mat4 m = glm::mat4(1.0f);

    if (node.matrix.size() == 16) {
        m[0][0] = static_cast<float>(node.matrix[0]);
        m[0][1] = static_cast<float>(node.matrix[1]);
        m[0][2] = static_cast<float>(node.matrix[2]);
        m[0][3] = static_cast<float>(node.matrix[3]);

        m[1][0] = static_cast<float>(node.matrix[4]);
        m[1][1] = static_cast<float>(node.matrix[5]);
        m[1][2] = static_cast<float>(node.matrix[6]);
        m[1][3] = static_cast<float>(node.matrix[7]);

        m[2][0] = static_cast<float>(node.matrix[8]);
        m[2][1] = static_cast<float>(node.matrix[9]);
        m[2][2] = static_cast<float>(node.matrix[10]);
        m[2][3] = static_cast<float>(node.matrix[11]);

        m[3][0] = static_cast<float>(node.matrix[12]);
        m[3][1] = static_cast<float>(node.matrix[13]);
        m[3][2] = static_cast<float>(node.matrix[14]);
        m[3][3] = static_cast<float>(node.matrix[15]);

        return m;
    }

    glm::vec3 t = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::quat r = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 s = glm::vec3(1.0f, 1.0f, 1.0f);

    if (node.translation.size() == 3) {
        t.x = static_cast<float>(node.translation[0]);
        t.y = static_cast<float>(node.translation[1]);
        t.z = static_cast<float>(node.translation[2]);
    }

    if (node.rotation.size() == 4) {
        r.w = static_cast<float>(node.rotation[3]);
        r.x = static_cast<float>(node.rotation[0]);
        r.y = static_cast<float>(node.rotation[1]);
        r.z = static_cast<float>(node.rotation[2]);
    }

    if (node.scale.size() == 3) {
        s.x = static_cast<float>(node.scale[0]);
        s.y = static_cast<float>(node.scale[1]);
        s.z = static_cast<float>(node.scale[2]);
    }

    glm::mat4 tm = glm::translate(glm::mat4(1.0f), t);
    glm::mat4 rm = glm::mat4_cast(r);
    glm::mat4 sm = glm::scale(glm::mat4(1.0f), s);

    m = tm * rm * sm;

    return m;
}

void GLBReader::ReadAccessorVec3Float(const tinygltf::Model& model, int accessorIndex, std::vector<Vector3f>& out) {
    if (accessorIndex < 0 or accessorIndex >= static_cast<int>(model.accessors.size())) {
        throw std::runtime_error("GLBReader: accessor index out of range");
    }

    tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(accessorIndex)];

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

    size_t base = view.byteOffset + acc.byteOffset;
    size_t need = base + stride * acc.count;

    if (need > buf.data.size()) {
        throw std::runtime_error("GLBReader: buffer overrun in ReadAccessorVec3Float");
    }

    out.resize(acc.count);

    for (size_t i = 0; i < acc.count; ++i) {
        size_t off = base + i * stride;

        float* f = reinterpret_cast<float*>(buf.data.data() + off);

        out[i] = Vector3f(f[0], f[1], f[2]);
    }
}

void GLBReader::ReadAccessorIndices(const tinygltf::Model& model, int accessorIndex, std::vector<uint32_t>& out) {
    if (accessorIndex < 0 or accessorIndex >= static_cast<int>(model.accessors.size())) {
        throw std::runtime_error("GLBReader: accessor index out of range");
    }

    tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(accessorIndex)];

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

    size_t base = view.byteOffset + acc.byteOffset;
    size_t need = base + stride * acc.count;

    if (need > buf.data.size()) {
        throw std::runtime_error("GLBReader: buffer overrun in ReadAccessorIndices");
    }

    out.resize(acc.count);

    for (size_t i = 0; i < acc.count; ++i) {
        size_t off = base + i * stride;
        uint8_t* p = buf.data.data() + off;

        if (elemSize == 1) {
            out[i] = static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(p));
        }
        else if (elemSize == 2) {
            out[i] = static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(p));
        }
        else {
            out[i] = static_cast<uint32_t>(*reinterpret_cast<uint32_t*>(p));
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
