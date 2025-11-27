#include "mesh.h"
#include "texturehandler.h"

// =================================================================================================

uint32_t Mesh::quadTriangleIndices[6] = { 0, 1, 2, 0, 2, 3 };

void Mesh::Init(GLenum shape, int32_t listSegmentSize) {
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

bool Mesh::CreateVAO(void) {
    if (m_vao)
        return true;
    if (not (m_vao = new VAO()))
        return false;
    return m_vao->Create(GL_QUADS, m_isDynamic);
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
    ManagedArray<Vector3f> vertices;
    vertices.Resize(m_vertices.AppDataLength());
    int i = 0;
    for (auto v : m_vertices.AppData())
        vertices[i++] = v;

    ManagedArray<Vector3f> normals;
    normals.Resize(m_vertices.AppDataLength());
    i = 0;
    for (auto n : m_normals.AppData())
        normals[i++] = n;

    ManagedArray<TexCoord> texCoords;
    texCoords.Resize(m_vertices.AppDataLength());
    i = 0;
    for (auto tc : m_texCoords[0].AppData())
        texCoords[i++] = tc;

    ManagedArray<Vector3f> tangents;
    tangents.Resize(m_vertices.AppDataLength());

    ManagedArray<Vector3f> bitangents;
    bitangents.Resize(m_vertices.AppDataLength());

    ManagedArray<GLuint>& indices = m_indices.GLData();

    for (int i = 0, l = indices.Length(); i < l;) {
        GLuint i0 = indices[i++];
        GLuint i1 = indices[i++];
        GLuint i2 = indices[i++];

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


bool Mesh::UpdateVAO(bool createVertexIndex, bool createTangents, bool forceUpdate) {
    if (not CreateVAO())
        return false;
    if (not createVertexIndex)
        createVertexIndex = (m_shape == GL_QUADS);
    m_vao->Create(createVertexIndex ? GL_TRIANGLES : m_shape, m_isDynamic);
    m_vao->Enable();
    m_tangents.SetDirty(m_vertices.IsDirty() or m_texCoords[0].IsDirty());
    if (createVertexIndex) {
        CreateVertexIndices();
        m_shape = GL_TRIANGLES;
        m_vao->m_indexBuffer.SetDynamic(true);
        UpdateIndexBuffer();
    }
    else if (m_indices.IsDirty(forceUpdate)) {
        m_indices.Setup();
        UpdateIndexBuffer();
    }
    if (m_vertices.IsDirty(forceUpdate)) {
        m_vertices.Setup();
        UpdateVertexBuffer();
        }
    int i = -1;
    for (auto& tc : m_texCoords) {
        if (tc.HaveData() and tc.IsDirty(forceUpdate)) {
            tc.Setup();
            UpdateTexCoordBuffer(++i);
        }
    }
    if (m_vertexColors.IsDirty(forceUpdate)) {
        m_vertexColors.Setup();
        UpdateColorBuffer();
    }
    if (m_normals.IsDirty(forceUpdate)) {
        m_normals.Setup();
        // in the case of an icosphere, the vertices also are the vertex normals
        UpdateNormalBuffer();
    }
    if (createTangents and m_tangents.IsDirty())
        UpdateTangents();
    if (m_floatBuffer.IsDirty(forceUpdate)) {
        m_floatBuffer.Setup();
        // in the case of an icosphere, the vertices also are the vertex normals
        UpdateFloatDataBuffer();
    }
    m_vao->Disable();
    return true;
}


void Mesh::ResetVAO(void) {
    m_indices.Reset();
    m_vertices.Reset();
    for (auto& tc : m_texCoords)
        tc.Reset();
    m_vertexColors.Reset();
    m_normals.Reset();
    if (m_vao)
        m_vao->Destroy();
}

void Mesh::SetupTexture(Texture* texture, String textureFolder, List<String> textureNames, GLenum textureType) {
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

bool Mesh::Render(Texture* texture) {
    if (not m_vao->IsValid())
        return false;
    m_vao->Render(texture);
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
    noexcept(m_vao->Destroy()))
{
    m_vertices.Destroy();
    m_normals.Destroy();
    for (auto& tc : m_texCoords)
        tc.Destroy();
    m_vertexColors.Destroy();
    m_indices.Destroy();
    m_textures.Clear();
    if (m_vao)
        m_vao->Destroy();
    delete m_vao;
    m_vao = nullptr;
    m_vMax = Vector3f{ -1e6, -1e6, -1e6 }; 
    m_vMin = Vector3f{ 1e6, 1e6, 1e6 }; 
}

// =================================================================================================
