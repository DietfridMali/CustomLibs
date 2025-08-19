#include "mesh.h"
#include "texturehandler.h"

// =================================================================================================

uint32_t Mesh::quadTriangleIndices[6] = { 0, 1, 2, 0, 2, 3 };

void Mesh::Init(GLenum shape, int32_t listSegmentSize, Texture* texture, String textureFolder, List<String> textureNames, GLenum textureType) {
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
    SetupTexture(texture, textureFolder, textureNames, textureType);
}


void Mesh::CreateVertexIndices(void) {
    uint32_t l = m_vertices.AppDataLength(); // number of quads
    uint32_t* pi = m_indices.m_glData.Resize((l / 2) * 3); // 6 indices for 4 vertices
    l /= 4; // quad count
    for (uint32_t i = 0, j = 0; i < l; i++, j += 4) {
        for (uint32_t k = 0; k < 6; k++)
            *pi++ = quadTriangleIndices[k] + j;
    }
}


void Mesh::UpdateVAO(bool createVertexIndex) {
    if (m_shape != GL_QUADS)
        createVertexIndex = false;
    m_vao.Init(createVertexIndex ? GL_TRIANGLES : m_shape);
    m_vao.Enable();
    if (m_indices.HaveData()) {
        m_indices.Setup();
        UpdateIndexBuffer();
    }
    else if (createVertexIndex) {
        CreateVertexIndices();
        m_vao.m_indexBuffer.SetDynamic(true);
        UpdateIndexBuffer();
    }
    if (m_vertices.HaveData()) {
        m_vertices.Setup();
        UpdateVertexBuffer();
    }
    if (m_texCoords.HaveData()) {
        m_texCoords.Setup();
        UpdateTexCoordBuffer();
    }
    if (m_vertexColors.HaveData()) {
        m_vertexColors.Setup();
        UpdateColorBuffer();
    }
    if (m_normals.HaveData()) {
        m_normals.Setup();
        // in the case of an icosphere, the vertices also are the vertex normals
        UpdateNormalBuffer();
    }
    m_vao.Disable();
}


void Mesh::ResetVAO(void) {
    m_indices.Reset();
    m_vertices.Reset();
    m_texCoords.Reset();
    m_vertexColors.Reset();
    m_normals.Reset();
}


void Mesh::SetupTexture(Texture* texture, String textureFolder, List<String> textureNames, GLenum textureType) {
    if (not textureNames.IsEmpty())
        m_textures += textureHandler.CreateByType (textureFolder, textureNames, textureType);
    else if (texture != nullptr)
        m_textures.Append(texture);
}


void Mesh::PushTexture(Texture* texture) {
    if (texture != nullptr)
        m_textures.Append(texture);
}


void Mesh::PopTexture(void) {
    if (not m_textures.IsEmpty()) {
        m_textures.DiscardLast();
    }
}


Texture* Mesh::GetTexture(void) {
    if (m_textures.Length())
        return m_textures.Last();
    return nullptr;
}


bool Mesh::EnableTexture(void) {
    Texture* texture = GetTexture();
    if (not texture)
        return false;
    texture->Enable();
    return true;
}


void Mesh::DisableTexture(void) {
    Texture* texture = GetTexture();
    if (texture)
        texture->Disable();
}


void Mesh::Render(Shader* shader, Texture* texture) {
    if (m_vao.IsValid()) {
#if 0
        SetTexture();
        SetColor();
        SetOutlineColor();
		SetMaxDistance(maxDistance);
#endif
        m_vao.Render(shader, texture);
    }
}


void Mesh::Destroy (void) {
    m_vertices.Destroy ();
    m_normals.Destroy ();
    m_texCoords.Destroy ();
    m_vertexColors.Destroy ();
    m_indices.Destroy ();
    m_textures.Clear ();
    m_vao.Destroy ();
}

// =================================================================================================
