#include "glew.h"
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

using VertexIndices = ManagedArray<GLuint>;

class IcoSphere 
    : public Mesh 
{
    public:
        GLuint          m_vertexCount;
        GLuint          m_faceCount;
        List<Vector3f>  m_faceNormals;

        class VertexKey {
        public:
            GLuint  i1, i2;
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

        IcoSphere(GLenum shape = GL_TRIANGLES) 
            : Mesh(false), m_vertexCount (0), m_faceCount (0) 
        {
            Mesh::Init (shape, 100);
            Mesh::SetName("IcoSphere");
        }

        IcoSphere(GLenum shape, Texture* texture, String textureFolder, List<String> textureNames) 
            : Mesh(false)
        {
            m_vertexCount = 0;
            m_faceCount = 0;
            Mesh::SetDynamic(false);
            Mesh::Init(shape, 100);
            Mesh::SetupTexture(texture, textureFolder, textureNames, GL_TEXTURE_CUBE_MAP);
        }

    protected:
        GLuint AddVertexIndices(Dictionary<VertexKey, GLuint>& indexLookup, GLuint i1, GLuint i2);

        List<Vector3f> CreateFaceNormals(VertexBuffer& vertices, SegmentedList<std::span<GLuint>>& faces);

};

// =================================================================================================
// Create an ico sphere based on a shape with triangular faces

class TriangleIcoSphere : public IcoSphere {
    public:
        TriangleIcoSphere(Texture* texture, String textureFolder, List<String> textureNames)
            : IcoSphere(GL_TRIANGLES, texture, textureFolder, textureNames)
        {}

        TriangleIcoSphere() : IcoSphere(GL_TRIANGLES) {}

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

class RectangleIcoSphere : public IcoSphere {
    public:
        RectangleIcoSphere(Texture * texture, String textureFolder, List<String> textureNames)
            : IcoSphere(GL_TRIANGLES, texture, textureFolder, textureNames)
        {}

        RectangleIcoSphere() : IcoSphere(GL_QUADS) {}

        void Create(int quality);

    protected:
        void CreateBaseMesh(int quality);

        void CreateCube(void);

        void CreateIcosahedron(void);
                
        void SubDivide(SegmentedList<VertexIndices>& faces);

        void Refine(SegmentedList<VertexIndices>& faces, int quality);

};

// =================================================================================================
