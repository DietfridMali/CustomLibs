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
    m_texCoords = TexCoordBuffer(listSegmentSize);
    m_vertexColors = ColorBuffer(listSegmentSize);
    m_indices = IndexBuffer(ShapeSize(), listSegmentSize);
}

bool Mesh::CreateVAO(void) {
    if (m_vao)
        return true;
    if (not (m_vao = new VAO()))
        return false;
    return m_vao->Create(GL_QUADS, false);
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

bool Mesh::UpdateVAO(bool createVertexIndex) {
    if (not CreateVAO())
        return false;
    if (not createVertexIndex)
        createVertexIndex = (m_shape == GL_QUADS);
    m_vao->Create(createVertexIndex ? GL_TRIANGLES : m_shape);
    m_vao->Enable();
    if (createVertexIndex) {
        CreateVertexIndices();
        m_shape = GL_TRIANGLES;
        m_vao->m_indexBuffer.SetDynamic(true);
        UpdateIndexBuffer();
    }
    else if (m_indices.IsDirty()) {
        m_indices.Setup();
        UpdateIndexBuffer();
    }
    if (m_vertices.IsDirty()) {
        m_vertices.Setup();
        UpdateVertexBuffer();
    }
    if (m_texCoords.IsDirty()) {
        m_texCoords.Setup();
        UpdateTexCoordBuffer();
    }
    if (m_vertexColors.IsDirty()) {
        m_vertexColors.Setup();
        UpdateColorBuffer();
    }
    if (m_normals.IsDirty()) {
        m_normals.Setup();
        // in the case of an icosphere, the vertices also are the vertex normals
        UpdateNormalBuffer();
    }
    m_vao->Disable();
    return true;
}

void Mesh::ResetVAO(void) {
    m_indices.Reset();
    m_vertices.Reset();
    m_texCoords.Reset();
    m_vertexColors.Reset();
    m_normals.Reset();
    if (m_vao)
        m_vao->Destroy();
}

void Mesh::SetupTexture(Texture* texture, String textureFolder, List<String> textureNames, GLenum textureType) {
    if (not textureNames.IsEmpty())
        m_textures += textureHandler.CreateByType(textureFolder, textureNames, textureType);
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
    noexcept(m_texCoords.Destroy()) &&
    noexcept(m_vertexColors.Destroy()) &&
    noexcept(m_indices.Destroy()) &&
    noexcept(m_textures.Clear()) &&
    noexcept(m_vao->Destroy()))
{
    m_vertices.Destroy();
    m_normals.Destroy();
    m_texCoords.Destroy();
    m_vertexColors.Destroy();
    m_indices.Destroy();
    m_textures.Clear();
    if (m_vao)
        m_vao->Destroy();
    m_vMax = Vector3f{ -1e6, -1e6, -1e6 }; 
    m_vMin = Vector3f{ 1e6, 1e6, 1e6 }; 
}

// =================================================================================================
