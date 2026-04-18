#include "mesh.h"
#include "texturehandler.h"

// =================================================================================================

uint32_t Mesh::quadTriangleIndices[6] = { 0, 1, 2, 0, 2, 3 };

void Mesh::Init(MeshTopology shape, int32_t listSegmentSize) {
    m_shape = shape;
    m_indices.m_componentCount = ShapeSize();
    //float f = std::numeric_limits<float>::lowest();
    m_vMax = Vector3f{ -1e6, -1e6, -1e6 }; // f, f, f);
    //f = std::numeric_limits<float>::max();
    m_vMin = Vector3f{ 1e6, 1e6, 1e6 }; // f, f, f);
    m_vertices = VertexBuffer(listSegmentSize);
    m_normals = VertexBuffer(listSegmentSize);
    for (auto& tc : m_texCoords)
        tc = TexCoordBuffer(listSegmentSize);
    m_vertexColors = ColorBuffer(listSegmentSize);
    m_indices = IndexBuffer(ShapeSize(), listSegmentSize);
}

bool Mesh::CreateLayout(void) {
    if (m_gfxDataLayout)
        return true;
    if (not (m_gfxDataLayout = new GfxDataLayout()))
        return false;
    return m_gfxDataLayout->Create(MeshTopology::Quads, m_isDynamic);
}

// This only works for linearly increasing quad vertex indices starting at 0!
void Mesh::CreateVertexIndices(void) {
    uint32_t l = m_vertices.AppDataLength(); // number of vertices
    uint32_t* pi = m_indices.GLData().Resize((l / 2) * 3); // 6 indices for 4 vertices
    l /= 4; // quad count
    for (uint32_t i = 0, j = 0; i < l; i++, j += 4) {
        for (uint32_t k = 0; k < 6; k++)
            *pi++ = quadTriangleIndices[k] + j;
    }
    m_indices.SetDirty(true);
}


void Mesh::UpdateTangents(void) {
    AutoArray<Vector3f> vertices;
    vertices.Resize(m_vertices.AppDataLength());
    int i = 0;
    for (auto v : m_vertices.AppData())
        vertices[i++] = v;

    AutoArray<Vector3f> normals;
    normals.Resize(m_vertices.AppDataLength());
    i = 0;
    for (auto n : m_normals.AppData())
        normals[i++] = n;

    AutoArray<TexCoord> texCoords;
    texCoords.Resize(m_vertices.AppDataLength());
    i = 0;
    for (auto tc : m_texCoords[0].AppData())
        texCoords[i++] = tc;

    AutoArray<Vector3f> tangents;
    tangents.Resize(m_vertices.AppDataLength());

    AutoArray<Vector3f> bitangents;
    bitangents.Resize(m_vertices.AppDataLength());

    AutoArray<uint32_t>& indices = m_indices.GLData();
    m_tangents.AppData().Reset();

    for (int i = 0, l = indices.Length(); i < l;) {
        uint32_t i0 = indices[i++];
        uint32_t i1 = indices[i++];
        uint32_t i2 = indices[i++];

        Vector3f edge1 = vertices[i1] - vertices[i0];
        Vector3f edge2 = vertices[i2] - vertices[i0];

        Vector2f deltaUV1 = texCoords[i1] - texCoords[i0];
        Vector2f deltaUV2 = texCoords[i2] - texCoords[i0];

        float det = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;
        if (det != 0.0f) {
            float r = 1.0f / det;

            Vector3f tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r;
            Vector3f bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * r;

            tangents[i0] += tangent;
            tangents[i1] += tangent;
            tangents[i2] += tangent;

            bitangents[i0] += bitangent;
            bitangents[i1] += bitangent;
            bitangents[i2] += bitangent;
        }
        // fallback for det == 0.0f see below
    }

    for (int i = 0; i < vertices.Length(); ++i) {
        Vector3f n = normals[i].Normal();
        Vector3f t = tangents[i];
        Vector3f b = bitangents[i];

        if (t.Dot(t) * b.Dot(b) == 0.0f) {
            Vector3f ref = (fabs(n.z) < 0.999f) ? Vector3f(0.0f, 0.0f, 1.0f) : Vector3f(0.0f, 1.0f, 0.0f);
            t = ref.Cross(n).Normalize();
            AddTangent(Vector4f(t, 1.0f));
        }
        else {
            t -= n * n.Dot(t);
            t.Normalize();
            float handedness = (n.Cross(t).Dot(b) < 0.0f) ? -1.0f : 1.0f;
            AddTangent(Vector4f(t, handedness));
        }
    }
    m_tangents.Setup();
    UpdateTangentBuffer();
}


bool Mesh::UpdateData(bool createVertexIndex, bool createTangents, bool forceUpdate) {
    if (not CreateLayout())
        return false;
    if (not createVertexIndex)
        createVertexIndex = (m_shape == MeshTopology::Quads);

    m_gfxDataLayout->Create(createVertexIndex ? MeshTopology::Triangles : m_shape, m_isDynamic);
    m_tangents.SetDirty((m_tangents.HaveData() and m_vertices.IsDirty()) or m_texCoords[0].IsDirty() or m_normals.IsDirty());

    auto updateBuffer = [&forceUpdate, this](auto& buffer, auto&& updateFn) {
        if (buffer.IsDirty(forceUpdate)) {
            buffer.Setup();
            updateFn( forceUpdate);
        }
    };

    auto updateBufferGroup = [&forceUpdate, this](auto& buffers, auto&& updateFn) {
        int i = 0;
        for (auto& b : buffers) {
            if (b.IsDirty(forceUpdate)) {
                b.Setup();
                updateFn(i, forceUpdate);
            }
            ++i;
        }
    };

    m_gfxDataLayout->StartUpdate();

    if (createVertexIndex) {
        CreateVertexIndices();
        m_shape = MeshTopology::Triangles;
        m_gfxDataLayout->m_indexBuffer.SetDynamic(true);
        UpdateIndexBuffer();
    }
    else {
        updateBuffer(m_indices, [this](bool f) { UpdateIndexBuffer(f); });
    }

    updateBuffer(m_vertices, [this](bool f) { UpdateVertexBuffer(f); });
    updateBufferGroup(m_texCoords, [this](int i, bool f) { UpdateTexCoordBuffer(i, f); });
    updateBuffer(m_vertexColors, [this](bool f) { UpdateColorBuffer(f); });
    updateBuffer(m_normals, [this](bool f) { UpdateNormalBuffer(f); });

    if (createTangents and m_tangents.IsDirty())
        UpdateTangents();

    updateBufferGroup(m_floatBuffers, [this](int i, bool f) { UpdateFloatDataBuffer(i, f); });
    updateBufferGroup(m_offsetBuffers, [this](int i, bool f) { UpdateOffsetBuffer(i, f); });

    m_gfxDataLayout->FinishUpdate();
    return true;
}


void Mesh::ResetGfxData(void) {
    m_indices.Reset();
    m_vertices.Reset();
    for (auto& tc : m_texCoords)
        tc.Reset();
    m_vertexColors.Reset();
    m_normals.Reset();
    if (m_gfxDataLayout)
        m_gfxDataLayout->Destroy();
}

void Mesh::SetupTexture(Texture* texture, String textureFolder, List<String> textureNames, TextureType textureType) {
    if (not textureNames.IsEmpty())
        m_textures += textureHandler.CreateByType(textureFolder, textureNames, textureType, {});
    else if (texture != nullptr)
        m_textures.Append(texture);
}

void Mesh::PushTexture(Texture* texture) {
    if (texture != nullptr)
        m_textures.Append(texture);
}

void Mesh::PopTexture(void) {
    if (not m_textures.IsEmpty()) 
        m_textures.DiscardLast();
}

Texture* Mesh::GetTexture(void)
noexcept
{
    if (m_textures.Length())
        return m_textures.Last();
    return nullptr;
}

bool Mesh::EnableTexture(void)
noexcept
{
    Texture* texture = GetTexture();
    if (not texture)
        return false;
    texture->Enable();
    return true;
}

void Mesh::DisableTexture(void)
noexcept
{
    Texture* texture = GetTexture();
    if (texture)
        texture->Disable();
}

bool Mesh::Render(std::span<Texture* const> textures, float alpha) {
    if (not m_gfxDataLayout->IsValid())
        return false;
    m_gfxDataLayout->Render(textures);
    return true;
}

void Mesh::Destroy(void)
noexcept(
 noexcept(m_vertices.Destroy()) &&
 noexcept(m_normals.Destroy()) &&
 noexcept(m_texCoords[0].Destroy()) &&
 noexcept(m_texCoords[1].Destroy()) &&
 noexcept(m_texCoords[2].Destroy()) &&
 noexcept(m_vertexColors.Destroy()) &&
 noexcept(m_indices.Destroy()) &&
 noexcept(m_textures.Clear()) &&
 noexcept(m_gfxDataLayout->Destroy()))
{
    m_vertices.Destroy();
    m_normals.Destroy();
    for (auto& b : m_texCoords)
        b.Destroy();
    for (auto& b : m_floatBuffers)
        b.Destroy();
    for (auto& b : m_offsetBuffers)
        b.Destroy();
    m_vertexColors.Destroy();
    m_indices.Destroy();
    m_textures.Clear();
    if (m_gfxDataLayout)
        m_gfxDataLayout->Destroy();
    delete m_gfxDataLayout;
    m_gfxDataLayout = nullptr;
    m_vMax = Vector3f{ -1e6, -1e6, -1e6 }; 
    m_vMin = Vector3f{ 1e6, 1e6, 1e6 }; 
}

// =================================================================================================
