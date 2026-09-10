#include "mesh.h"
#include "texturehandler.h"
#include "tracy_wrapper.h"

// =================================================================================================

uint32_t Mesh::quadTriangleIndices[6] = { 0, 2, 1, 0, 3, 2 };

void Mesh::Init(MeshTopology shape, int32_t listSegmentSize) {
    m_shape = shape;
    m_indices.m_componentCount = ShapeSize();
    //float f = (std::numeric_limits<float>::lowest)();
    m_vMax = Vector3f{ -1e6, -1e6, -1e6 }; // f, f, f);
    //f = (std::numeric_limits<float>::max)();
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
    return m_gfxDataLayout->Create(MeshTopology::Quads, m_dynamicBuffers);
}

// This only works for linearly increasing quad vertex indices starting at 0!
void Mesh::CreateVertexIndices(void) {
    uint32_t l = m_vertices.AppDataLength(); // number of vertices
    uint32_t* pi = m_indices.GfxData().Resize((l / 2) * 3); // 6 indices for 4 vertices
    l /= 4; // quad count
    for (uint32_t i = 0, j = 0; i < l; i++, j += 4) {
        for (uint32_t k = 0; k < 6; k++)
            *pi++ = quadTriangleIndices[k] + j;
    }
    m_indices.SetDirty(true);
}


// Computed from the GFX data, not the app data: a mesh may be filled through its gfx data directly
// (a level mesh with a hundred thousand vertices does that), and one filled through the app data has
// been packed by UpdateData () by the time this runs. The result goes into the gfx data the same way;
// Setup () leaves it alone as long as the tangent app data stays empty.
void Mesh::UpdateTangents(void) {
    AutoArray<float>& vertexData = m_vertices.GfxData();
    AutoArray<float>& normalData = m_normals.GfxData();
    AutoArray<float>& texCoordData = m_texCoords[0].GfxData();
    const uint32_t vertexCount = uint32_t(vertexData.Length()) / 3;

    if ((vertexCount == 0) or (uint32_t(normalData.Length()) < vertexCount * 3) or (uint32_t(texCoordData.Length()) < vertexCount * 2))
        return;

    auto vertexAt = [&vertexData](uint32_t i) {
        return Vector3f(vertexData[i * 3], vertexData[i * 3 + 1], vertexData[i * 3 + 2]);
    };
    auto normalAt = [&normalData](uint32_t i) {
        return Vector3f(normalData[i * 3], normalData[i * 3 + 1], normalData[i * 3 + 2]);
    };
    auto texCoordAt = [&texCoordData](uint32_t i) {
        return Vector2f(texCoordData[i * 2], texCoordData[i * 2 + 1]);
    };

    AutoArray<Vector3f> tangents;
    tangents.Resize(int32_t(vertexCount));

    AutoArray<Vector3f> bitangents;
    bitangents.Resize(int32_t(vertexCount));

    AutoArray<uint32_t>& indices = m_indices.GfxData();

    for (int i = 0, l = indices.Length(); i + 2 < l;) {
        uint32_t i0 = indices[i++];
        uint32_t i1 = indices[i++];
        uint32_t i2 = indices[i++];

        Vector3f v0 = vertexAt(i0);
        Vector3f edge1 = vertexAt(i1) - v0;
        Vector3f edge2 = vertexAt(i2) - v0;

        Vector2f uv0 = texCoordAt(i0);
        Vector2f deltaUV1 = texCoordAt(i1) - uv0;
        Vector2f deltaUV2 = texCoordAt(i2) - uv0;

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

    m_tangents.AppData().Reset();
    float* pTangent = m_tangents.GfxData().Resize(int32_t(vertexCount) * 4);
    if (not pTangent)
        return;

    for (uint32_t i = 0; i < vertexCount; ++i) {
        Vector3f n = normalAt(i).Normal();
        Vector3f t = tangents[i];
        Vector3f b = bitangents[i];
        float handedness = 1.0f;

        if (t.Dot(t) * b.Dot(b) == 0.0f) {
            Vector3f ref = (fabs(n.z) < 0.999f) ? Vector3f(0.0f, 0.0f, 1.0f) : Vector3f(0.0f, 1.0f, 0.0f);
            t = ref.Cross(n).Normalize();
        }
        else {
            t -= n * n.Dot(t);
            t.Normalize();
            handedness = (n.Cross(t).Dot(b) < 0.0f) ? -1.0f : 1.0f;
        }
        *pTangent++ = t.x;
        *pTangent++ = t.y;
        *pTangent++ = t.z;
        *pTangent++ = handedness;
    }
    m_tangents.Setup();
    UpdateTangentBuffer();
}


bool Mesh::UpdateData(bool createVertexIndex, bool createTangents, bool forceUpdate) {
    if (not CreateLayout())
        return false;
    if (not createVertexIndex)
        createVertexIndex = (m_shape == MeshTopology::Quads);

    m_gfxDataLayout->Create(createVertexIndex ? MeshTopology::Triangles : m_shape, m_dynamicBuffers);
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
    updateBufferGroup(m_uintBuffers, [this](int i, bool f) { UpdateUintDataBuffer(i, f); });

    m_gfxDataLayout->FinishUpdate();

    // Rebuild the buffer-composition mask from the buffers that actually carry data.
    m_meshBufferMask = 0;
    if (m_indices.HaveData())
        m_meshBufferMask |= mbIndex;
    if (m_vertices.HaveData())
        m_meshBufferMask |= mbVertex;
    for (int i = 0; i < 3; ++i)
        if (m_texCoords[i].HaveData())
            m_meshBufferMask |= (uint32_t(mbTexCoord0) << i);
    if (m_vertexColors.HaveData())
        m_meshBufferMask |= mbColor;
    if (m_normals.HaveData())
        m_meshBufferMask |= mbNormal;
    if (m_tangents.HaveData())
        m_meshBufferMask |= mbTangent;
    for (int i = 0; (i < m_floatBuffers.Length()) and (i < 2); ++i)
        if (m_floatBuffers[i].HaveData())
            m_meshBufferMask |= (uint32_t(mbFloat0) << i);
    for (int i = 0; (i < m_offsetBuffers.Length()) and (i < 4); ++i)
        if (m_offsetBuffers[i].HaveData())
            m_meshBufferMask |= (uint32_t(mbOffset0) << i);
    for (int i = 0; (i < m_uintBuffers.Length()) and (i < 2); ++i)
        if (m_uintBuffers[i].HaveData())
            m_meshBufferMask |= (uint32_t(mbUint0) << i);

    return true;
}


void Mesh::ResetGfxData(void) {
    // Reset the CPU-side buffers only. The GfxDataLayout and its GPU buffers stay alive
    // so the next UpdateData reuses them instead of reallocating from scratch.
    m_indices.Reset();
    m_vertices.Reset();
    for (auto& tc : m_texCoords)
        tc.Reset();
    m_vertexColors.Reset();
    m_normals.Reset();
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
    texture->Activate({});
    return true;
}

void Mesh::DisableTexture(void)
noexcept
{
    Texture* texture = GetTexture();
    if (texture)
        texture->Deactivate();
}

bool Mesh::Render(std::span<Texture* const> textures, float alpha) {
    ZoneScoped;
    if (not m_gfxDataLayout->IsValid())
        return false;
    m_gfxDataLayout->Render(textures);
    return true;
}


bool Mesh::RenderRange(uint32_t firstIndex, uint32_t indexCount, std::span<Texture* const> textures) {
    ZoneScoped;
    if ((m_gfxDataLayout == nullptr) or not m_gfxDataLayout->IsValid())
        return false;
    m_gfxDataLayout->Render(textures, firstIndex, indexCount);
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
    for (auto& b : m_uintBuffers)
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
