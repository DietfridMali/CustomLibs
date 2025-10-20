#pragma once
#define NOMINMAX

#include <limits>

#include "glew.h"
#include "sharedpointer.hpp"
#include "texture.h"
#include "vao.h"
#include "vertexdatabuffers.h"

// =================================================================================================
// Mesh class definitions for basic mesh information, allowing to pass child classes to functions
// operating with or by meshes
// A mesh is defined by a set of faces, which in turn are defined by vertices, and a set of textures
// The only mesh used in Smiley Battle are ico spheres

class AbstractMesh {
public:
    virtual void Create(int quality, Texture* texture, List<String> textureNames) = 0;

    virtual void Destroy(void) = 0;

    virtual bool Render(Texture* texture) = 0;

    virtual ~AbstractMesh() = default;
};

// =================================================================================================

class MeshColors {
public:
    List<RGBAColor> m_colors;

    void Push(RGBAColor color) {
        m_colors.Append(color);
    }

    void Pop(void) {
        m_colors.Extract(-1);
    }

    RGBAColor Get(void) {
        if (not m_colors.IsEmpty())
            return m_colors[-1];
        return RGBAColor{ 1, 1, 1, 1 };
    }

    void Destroy(void) {
        m_colors.Clear();
    }
};


class Mesh 
    : public AbstractMesh
{
public:
    String              m_name{ "" };
    TextureList         m_textures;
    VertexBuffer        m_vertices;
    VertexBuffer        m_normals;
    TexCoordBuffer      m_texCoords[2];
    ColorBuffer         m_vertexColors;
    IndexBuffer         m_indices;
    VAO*                m_vao{ nullptr };
    GLenum              m_shape{ 0 };
    Vector3f            m_vMin{ Vector3f::ZERO };
    Vector3f            m_vMax{ Vector3f::ZERO };
    bool                m_isDynamic{ false };

    static uint32_t quadTriangleIndices[6];

    Mesh(bool isDynamic = true) {
        SetDynamic(isDynamic);
    }

    ~Mesh() {
        Destroy();
    }

    void Init(GLenum shape, int32_t listSegmentSize);

    bool CreateVAO(void);

    void SetName(String name) { m_name = name; }

    String GetName(void) { return m_name; }

    virtual void Create(int quality, Texture* texture, List<String> textureNames) {}

    virtual void Destroy(void);

    inline void SetDynamic(bool isDynamic) {
        m_isDynamic = isDynamic;
        if (m_vao)
            m_vao->SetDynamic(isDynamic);
    }

    inline void SetShape(GLenum shape) noexcept {
        m_shape = shape;
        if (m_vao)
            m_vao->SetShape(shape);
    }

    inline uint32_t ShapeSize(void)
        noexcept
    {
        if (m_shape == GL_QUADS)
            return 4;
        if (m_shape == GL_TRIANGLES)
            return 3;
        if (m_shape == GL_LINES)
            return 2;
        return 1;
    }

    inline VertexBuffer& Vertices(void) noexcept { return m_vertices; }

    inline VertexBuffer& Normals(void) noexcept { return m_normals; }

    inline TexCoordBuffer& TexCoords(int i) noexcept { return m_texCoords [i]; }

    inline ColorBuffer& VertexColors(void) noexcept { return m_vertexColors; }

    inline IndexBuffer& Indices(void) noexcept { return m_indices; }

    inline void UpdateVertexBuffer(void) {
        if (m_vao)
            m_vao->UpdateVertexBuffer("Vertex", 0, m_vertices, GL_FLOAT);
    }

    inline void UpdateTexCoordBuffer(int i) {
        if (m_vao)
            m_vao->UpdateVertexBuffer("TexCoord", i, m_texCoords[i], GL_FLOAT);
    }

    inline void UpdateColorBuffer(void) {
        if (m_vao)
            m_vao->UpdateVertexBuffer("Color", 0, m_vertexColors, GL_FLOAT);
    }

    // in the case of an icosphere, the vertices also are the vertex normals
    inline void UpdateNormalBuffer(void) {
        if (m_vao)
            m_vao->UpdateVertexBuffer("Normal", 0, m_normals, GL_FLOAT);
    }

    inline void UpdateIndexBuffer(void) {
        if (m_vao)
            m_vao->UpdateIndexBuffer(m_indices, GL_UNSIGNED_INT);
    }

    bool UpdateVAO(bool createVertexIndex = false);

    void ResetVAO(void);

    void CreateVertexIndices(void);

    inline VAO* GetVAO(void) noexcept
    {
        return m_vao;
    }

    void SetupTexture(Texture* texture, String textureFolder = "", List<String> textureNames = List<String>(), GLenum textureType = GL_TEXTURE_2D);

    virtual void PushTexture(Texture* texture);

    virtual void PopTexture(void);

    virtual Texture* GetTexture(void)
        noexcept;

    bool EnableTexture(void)
        noexcept;

    void DisableTexture(void)
        noexcept;

    inline void AddVertex(const Vector3f& v) {
        m_vertices.Append(v);
        m_vMin.Minimize(v);
        m_vMax.Maximize(v);
    }

    inline void AddVertex(Vector3f& v) {
        AddVertex(static_cast<const Vector3f&>(v));
    }

    inline void AddTexCoord(const TexCoord& tc, int i = 0) {
        m_texCoords[i].Append(tc);
    }

    inline void AddTexCoord(TexCoord&& tc, int i = 0) {
        m_texCoords[i].Append(const_cast<const TexCoord&>(tc));
    }

    inline void AddTexCoord(const SegmentedList<TexCoord>& tc, int i = 0) {
        m_texCoords[i].Append(tc);
    }

    inline void AddColor(const RGBAColor& c) {
        m_vertexColors.Append(c);
    }

    inline void AddColor(RGBAColor&& c) {
        m_vertexColors.Append(static_cast<const RGBAColor&>(c));
    }

    inline void AddNormal(const Vector3f& n) {
        m_normals.Append(n);
    }

    inline void AddNormal(Vector3f&& n) {
        m_normals.Append(static_cast<const Vector3f&>(n));
    }

    inline void AddIndices(ManagedArray<GLuint>& i) {
        m_indices.Append(i);
    }

    inline void SetIndices(ManagedArray<GLuint>& i) {
        m_indices.SetGLData(i);
    }

    inline bool IsEmpty(void)
        noexcept(noexcept(m_vertices.IsEmpty()))
    {
        return m_vertices.IsEmpty();
    }

    virtual bool Render(Texture* texture);
};

// =================================================================================================
