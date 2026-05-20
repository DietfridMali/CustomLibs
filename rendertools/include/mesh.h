#pragma once
#define NOMINMAX

#include <limits>
#include <cstring>

#include "rendertypes.h"
#include "sharedpointer.hpp"
#include "texture.h"
#include "gfxdatalayout.h"
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

// =================================================================================================

class Mesh
    : public AbstractMesh
{
public:
    // Buffer-composition bitmask: one bit per potential mesh data buffer, ordered as Mesh::UpdateData
    // processes them. Mesh::m_meshBufferMask carries the composition of a concrete mesh; MeshHandler
    // matches pooled meshes on it, so a recycled mesh always has exactly the requested buffer set.
    enum eMeshBufferBits : uint32_t {
        mbIndex     = 1u << 0,
        mbVertex    = 1u << 1,
        mbTexCoord0 = 1u << 2,
        mbTexCoord1 = 1u << 3,
        mbTexCoord2 = 1u << 4,
        mbColor     = 1u << 5,
        mbNormal    = 1u << 6,
        mbTangent   = 1u << 7,
        mbOffset0   = 1u << 8,
        mbOffset1   = 1u << 9,
        mbOffset2   = 1u << 10,
        mbOffset3   = 1u << 11,
        mbFloat0    = 1u << 12,
        mbFloat1    = 1u << 13
    };

    // Maps a GfxDataBuffer's (type, id) tag to its eMeshBufferBits bit; 0 if unknown.
    static uint32_t MeshBufferBit(const char* type, int id) {
        if (not strcmp(type, "Index"))
            return mbIndex;
        if (not strcmp(type, "Vertex"))
            return mbVertex;
        if (not strcmp(type, "TexCoord"))
            return ((id >= 0) and (id <= 2)) ? (uint32_t(mbTexCoord0) << id) : 0u;
        if (not strcmp(type, "Color"))
            return mbColor;
        if (not strcmp(type, "Normal"))
            return mbNormal;
        if (not strcmp(type, "Tangent"))
            return mbTangent;
        if (not strcmp(type, "Float"))
            return ((id >= 0) and (id <= 1)) ? (uint32_t(mbFloat0) << id) : 0u;
        if (not strcmp(type, "Offset"))
            return ((id >= 0) and (id <= 3)) ? (uint32_t(mbOffset0) << id) : 0u;
        return 0u;
    }

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
    GfxDataLayout* m_gfxDataLayout{ nullptr };
    MeshTopology                    m_shape{ MeshTopology::Quads };
    Vector3f                        m_vMin{ Vector3f::ZERO };
    Vector3f                        m_vMax{ Vector3f::ZERO };
    uint32_t                        m_dynamicBuffers{ 0 };   // eMeshBufferBits: buffers needing dynamic treatment
    uint32_t                        m_meshBufferMask{ 0 };   // eMeshBufferBits, rebuilt in UpdateData

    static uint32_t quadTriangleIndices[6];

    Mesh(uint32_t dynamicBuffers = 0) {
        SetDynamic(dynamicBuffers);
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

    inline void SetDynamic(uint32_t dynamicBuffers) {
        m_dynamicBuffers = dynamicBuffers;
        if (m_gfxDataLayout)
            m_gfxDataLayout->SetDynamic(dynamicBuffers);
    }

    inline void SetShape(MeshTopology shape) noexcept {
        m_shape = shape;
        if (m_gfxDataLayout)
            m_gfxDataLayout->SetShape(shape);
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
        if (m_gfxDataLayout)
            m_gfxDataLayout->UpdateDataBuffer("Vertex", 0, m_vertices, ComponentType::Float, forceUpdate);
    }

    inline void UpdateTexCoordBuffer(int i, bool forceUpdate = false) {
        if (m_gfxDataLayout)
            m_gfxDataLayout->UpdateDataBuffer("TexCoord", i, m_texCoords[i], ComponentType::Float, forceUpdate);
    }

    inline void UpdateTangentBuffer(bool forceUpdate = false) {
        if (m_gfxDataLayout)
            m_gfxDataLayout->UpdateDataBuffer("Tangent", 0, m_tangents, ComponentType::Float, forceUpdate);
    }

    inline void UpdateColorBuffer(bool forceUpdate = false) {
        if (m_gfxDataLayout)
            m_gfxDataLayout->UpdateDataBuffer("Color", 0, m_vertexColors, ComponentType::Float, forceUpdate);
    }

    // in the case of an icosphere, the vertices also are the vertex normals
    inline void UpdateNormalBuffer(bool forceUpdate = false) {
        if (m_gfxDataLayout)
            m_gfxDataLayout->UpdateDataBuffer("Normal", 0, m_normals, ComponentType::Float, forceUpdate);
    }

    inline void UpdateFloatDataBuffer(int i, bool forceUpdate = false) {
        if (m_gfxDataLayout)
            m_gfxDataLayout->UpdateDataBuffer("Float", i, m_floatBuffers[i], ComponentType::Float, forceUpdate);
    }

    inline void UpdateOffsetBuffer(int i, bool forceUpdate = false) {
        if (m_gfxDataLayout)
            m_gfxDataLayout->UpdateDataBuffer("Offset", i, m_offsetBuffers[i], ComponentType::Float, forceUpdate);
    }

    inline void UpdateIndexBuffer(bool forceUpdate = false) {
        if (m_gfxDataLayout)
            m_gfxDataLayout->UpdateIndexBuffer(m_indices, ComponentType::UInt32, forceUpdate);
    }

    bool UpdateData(bool createVertexIndex = false, bool createTangents = false, bool forceUpdate = false);

    void UpdateTangents(void);

    void ResetGfxData(void);

    void CreateVertexIndices(void);

    inline GfxDataLayout* GetGfxDataLayout(void) noexcept
    {
        return m_gfxDataLayout;
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
        noexcept(noexcept(m_vertices.IsEmpty())) {
        return m_vertices.IsEmpty();
    }

    virtual bool Render(std::span<Texture* const> textures = {}, float alpha = 1.0);

    inline bool Render(Texture* texture = nullptr, float alpha = 1.0) {
        return Render(texture ? std::span<Texture* const>(&texture, 1) : std::span<Texture* const>{}, alpha);
    }

    inline bool Render(std::initializer_list<Texture*> textures, float alpha = 1.0) {
        return Render(std::span<Texture* const>(textures.begin(), textures.size()), alpha);
    }
};

// =================================================================================================
