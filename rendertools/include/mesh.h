#pragma once
#define NOMINMAX

#include <limits>

#include "rendertypes.h"
#include "sharedpointer.hpp"
#include "texture.h"
#include "gfxdatalayout.h.h"
#include "vertexdatabuffers.h"

// =================================================================================================
// Mesh class definitions for basic mesh information, allowing to pass child classes to functions
// operating with or by meshes
// A mesh is defined by a set of faces, which in turn are defined by vertices, and a set of textures
// The only mesh used in Smiley Battle are ico spheres

class AbstractMesh {
public:
    virtual bool Create(void) = 0;

    virtual void Destroy(void) = 0;

    virtual bool Update(void) = 0;

    virtual bool Render(std::span<Texture* const> textures, float alpha = 1.0) = 0;

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
    String                          m_name{ "" };
    TextureList                     m_textures;
    VertexBuffer                    m_vertices;
    VertexBuffer                    m_normals;
    TangentBuffer                   m_tangents;
    StaticArray<TexCoordBuffer, 3>  m_texCoords;
    ColorBuffer                     m_vertexColors;
    IndexBuffer                     m_indices;
    List<FloatDataBuffer>           m_floatBuffers;
    List<VertexBuffer>              m_offsetBuffers;
    gfxdatalayout.h* m_gfxdatalayout.h{ nullptr };
    MeshTopology                    m_shape{ MeshTopology::Quads };
    Vector3f                        m_vMin{ Vector3f::ZERO };
    Vector3f                        m_vMax{ Vector3f::ZERO };
    bool                            m_isDynamic{ false };

    static uint32_t quadTriangleIndices[6];

    Mesh(bool isDynamic = true) {
        SetDynamic(isDynamic);
    }

    ~Mesh() {
        Destroy();
    }

    void Init(MeshTopology shape, int32_t listSegmentSize);

    bool CreateLayout(void);

    void SetName(String name) { m_name = name; }

    String GetName(void) { return m_name; }

    virtual bool Create(void) { return true; }

    virtual bool Update(void) { return true; }

    virtual void Destroy(void);

    inline void SetDynamic(bool isDynamic) {
        m_isDynamic = isDynamic;
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->SetDynamic(isDynamic);
    }

    inline void SetShape(MeshTopology shape) noexcept {
        m_shape = shape;
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->SetShape(shape);
    }

    inline uint32_t ShapeSize(void)
 noexcept
    {
        if (m_shape == MeshTopology::Quads)
            return 4;
        if (m_shape == MeshTopology::Triangles)
            return 3;
        if (m_shape == MeshTopology::Lines)
            return 2;
        return 1;
    }

    inline VertexBuffer& Vertices(void) noexcept { return m_vertices; }

    inline VertexBuffer& Normals(void) noexcept { return m_normals; }

    inline TexCoordBuffer& TexCoords(int i) noexcept { return m_texCoords[i]; }

    inline TangentBuffer& Tangents(void) noexcept { return m_tangents; }

    inline ColorBuffer& VertexColors(void) noexcept { return m_vertexColors; }

    inline IndexBuffer& Indices(void) noexcept { return m_indices; }

    inline FloatDataBuffer& FloatBuffer(int i) noexcept { return m_floatBuffers[i]; }

    inline VertexBuffer& OffsetBuffer(int i) noexcept { return m_offsetBuffers[i]; }

    inline void UpdateVertexBuffer(bool forceUpdate = false) {
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->UpdateDataBuffer("Vertex", 0, m_vertices, ComponentType::Float, forceUpdate);
    }

    inline void UpdateTexCoordBuffer(int i, bool forceUpdate = false) {
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->UpdateDataBuffer("TexCoord", i, m_texCoords[i], ComponentType::Float, forceUpdate);
    }

    inline void UpdateTangentBuffer(bool forceUpdate = false) {
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->UpdateDataBuffer("Tangent", 0, m_tangents, ComponentType::Float, forceUpdate);
    }

    inline void UpdateColorBuffer(bool forceUpdate = false) {
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->UpdateDataBuffer("Color", 0, m_vertexColors, ComponentType::Float, forceUpdate);
    }

    // in the case of an icosphere, the vertices also are the vertex normals
    inline void UpdateNormalBuffer(bool forceUpdate = false) {
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->UpdateDataBuffer("Normal", 0, m_normals, ComponentType::Float, forceUpdate);
    }

    inline void UpdateFloatDataBuffer(int i, bool forceUpdate = false) {
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->UpdateDataBuffer("Float", i, m_floatBuffers[i], ComponentType::Float, forceUpdate);
    }

    inline void UpdateOffsetBuffer(int i, bool forceUpdate = false) {
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->UpdateDataBuffer("Offset", i, m_offsetBuffers[i], ComponentType::Float, forceUpdate);
    }

    inline void UpdateIndexBuffer(bool forceUpdate = false) {
        if (m_gfxdatalayout.h)
            m_gfxdatalayout.h->UpdateIndexBuffer(m_indices, ComponentType::UInt32, forceUpdate);
    }

    bool UpdateData(bool createVertexIndex = false, bool createTangents = false, bool forceUpdate = false);

    void UpdateTangents(void);

    void ResetGfxData(void);

    void CreateVertexIndices(void);

    inline gfxdatalayout.h* Getgfxdatalayout.h(void) noexcept
    {
        return m_gfxdatalayout.h;
    }

    void SetupTexture(Texture* texture, String textureFolder = "", List<String> textureNames = List<String>(), TextureType textureType = TextureType::Texture2D);

    virtual void PushTexture(Texture* texture);

    virtual void PopTexture(void);

    virtual Texture* GetTexture(void)
 noexcept;

    bool EnableTexture(void)
 noexcept;

    void DisableTexture(void)
 noexcept;

    inline void AddFloatBuffer(void) noexcept {
        m_floatBuffers.Append();
    }

    inline void ResetFloatBuffers(void) noexcept {
        m_floatBuffers.Clear();
    }

    inline void AddOffsetBuffer(void) noexcept {
        m_offsetBuffers.Append();
    }

    inline void ResetOffsetBuffers(void) noexcept {
        m_offsetBuffers.Clear();
    }

    inline void AddVertex(const Vector3f& v) {
        m_vertices.Append(v);
        m_vMin.Minimize(v);
        m_vMax.Maximize(v);
    }

    inline void AddVertex(Vector3f& v) {
        AddVertex(static_cast<const Vector3f&>(v));
    }

    inline void AddTangent(const Vector4f& v) {
        m_tangents.Append(v);
    }

    inline void AddTangent(Vector4f& v) {
        AddTangent(static_cast<const Vector4f&>(v));
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

    inline void AddIndex(uint32_t i) {
        m_indices.Append(i);
    }

    inline void AddIndices(AutoArray<uint32_t>& i) {
        m_indices.Append(i);
    }

    inline void SetIndices(AutoArray<uint32_t>& i) {
        m_indices.SetGLData(i);
    }

    inline void AddFloat(int i, const float n) {
        m_floatBuffers[i].Append(n);
    }

    inline bool IsEmpty(void)
 noexcept(noexcept(m_vertices.IsEmpty()))
    {
        return m_vertices.IsEmpty();
    }

    virtual bool Render(std::span<Texture* const> textures = {}, float alpha = 1.0);

    inline bool Render(Texture* texture = nullptr, float alpha = 1.0) {
        return Render(texture ? std::span<Texture* const>(&texture, 1) : std::span<Texture* const>{}, alpha);
    }
};

// =================================================================================================
