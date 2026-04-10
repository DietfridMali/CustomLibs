#include "vector.hpp"
#include "texture.h"
#include "vertexdatabuffers.h"
#include "mesh.h"
#include "string.hpp"
#include "dictionary.hpp"
#include "segmentedlist.hpp"

// =================================================================================================
// Basic ico sphere class.
// Ico spheres are created from basic geometric structures with equidistant corners (vertices},
// e.g. cubes, octa- or icosahedrons.
// The faces of the basic structures are subdivided in equally sized child faces. The resulting new
// vertices are normalized. The more iterations this is run through, the finer the resulting mesh
// becomes and the smoother does the sphere look.

using VertexIndices = AutoArray<uint32_t>;

class IcoSphere
    : public Mesh
{
public:
    uint32_t        m_vertexCount;
    uint32_t        m_faceCount;
    List<Vector3f>  m_faceNormals;

    class VertexKey {
    public:
        uint32_t  i1, i2;
#if (USE_STD || USE_STD_MAP)
        bool operator<(const VertexKey& other) const noexcept {
            // Lexikografischer Vergleich wie in KeyCmp:
            if (i1 < other.i1)
                return true;
            if (i1 > other.i1)
                return false;
            return i2 < other.i2;
        }
#endif
    };

public:
#if !(USE_STD || USE_STD_MAP)
    static int KeyCmp(void* context, VertexKey const& ki, VertexKey const& kj) noexcept {
        return (ki.i1 < kj.i1) ? -1 : (ki.i1 > kj.i1) ? 1 : (ki.i2 < kj.i2) ? -1 : (ki.i2 > kj.i2) ? 1 : 0;
    }
#endif

    IcoSphere(MeshTopology shape = MeshTopology::Triangles)
        : Mesh(false), m_vertexCount(0), m_faceCount(0)
    {
        Mesh::Init(shape, 100);
        Mesh::SetName("IcoSphere");
    }

    IcoSphere(MeshTopology shape, Texture* texture, String textureFolder, List<String> textureNames)
        : Mesh(false)
    {
        m_vertexCount = 0;
        m_faceCount = 0;
        Mesh::SetDynamic(false);
        Mesh::Init(shape, 100);
        Mesh::SetupTexture(texture, textureFolder, textureNames, TextureType::CubeMap);
    }

    void Destroy(void) {
        Mesh::Destroy();
        m_faceNormals.Clear();
    }

protected:
    uint32_t AddVertexIndices(Dictionary<VertexKey, uint32_t>& indexLookup, uint32_t i1, uint32_t i2);

    List<Vector3f> CreateFaceNormals(VertexBuffer& vertices, SegmentedList<std::span<uint32_t>>& faces);

};

// =================================================================================================
// Create an ico sphere based on a shape with triangular faces

class TriangleIcoSphere
    : public IcoSphere {
public:
    TriangleIcoSphere(Texture* texture, String textureFolder, List<String> textureNames)
        : IcoSphere(MeshTopology::Triangles, texture, textureFolder, textureNames)
    {
    }

    TriangleIcoSphere() : IcoSphere(MeshTopology::Triangles) {}

    void Create(int quality);

protected:
    void CreateBaseMesh(int quality = 1);

    void CreateOctahedron(void);

    void CreateIcosahedron(void);

    void SubDivide(SegmentedList<VertexIndices>& faces);

    void Refine(SegmentedList<VertexIndices>& faces, int quality);

};

// =================================================================================================
// Create an ico sphere based on a shape with rectangular faces

class RectangleIcoSphere
    : public IcoSphere {
public:
    RectangleIcoSphere(Texture* texture, String textureFolder, List<String> textureNames)
        : IcoSphere(MeshTopology::Triangles, texture, textureFolder, textureNames)
    {
    }

    RectangleIcoSphere() : IcoSphere(MeshTopology::Quads) {}

    void Create(int quality);

protected:
    void CreateBaseMesh(int quality);

    void CreateTriangleVertexIndices(void);

    void CreateCube(void);

    void CreateIcosahedron(void);

    void SubDivide(SegmentedList<VertexIndices>& faces);

    void Refine(SegmentedList<VertexIndices>& faces, int quality);

};

// =================================================================================================
