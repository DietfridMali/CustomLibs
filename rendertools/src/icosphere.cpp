#include "icosphere.h"

// =================================================================================================
// Basic ico sphere class.
// Ico spheres are created from basic geometric structures with equidistant corners (vertices},
// e.g. cubes, octa- or icosahedrons.
// The faces of the basic structures are subdivided in equally sized child faces. The resulting new
// vertices are normalized. The more iterations this is run through, the finer the resulting mesh
// becomes and the smoother does the sphere look.

GLuint IcoSphere::AddVertexIndices(Dictionary<VertexKey, GLuint>& indexLookup, GLuint i1, GLuint i2) { // find index pair i1,i2 in 
    VertexKey key;
    if (i1 < i2)
        key = { i1, i2 };
    else
        key = { i2, i1 };
    GLuint* keyPtr = indexLookup.Find(key);
    if (keyPtr)
        return *keyPtr;
    indexLookup.Insert(key, m_vertexCount);
    Vector3f v = m_vertices [int(i1)] + m_vertices [int(i2)];
    v.Normalize();
    v *= 0.5f;
    m_vertices.m_appData.Append(v);
    return m_vertexCount++;
}


List<Vector3f> IcoSphere::CreateFaceNormals(VertexBuffer& vertices, SegmentedList<std::span<GLuint>>& faces) {
    List<Vector3f> faceNormals;
    for (auto& f : faces)
        faceNormals.Append(Vector3f::Normal(vertices[f[0]], vertices[f[1]], vertices[f[2]]));
    return faceNormals;
}

// =================================================================================================
// Create an ico sphere based on a shape with triangular faces

void TriangleIcoSphere::Create(int quality) {
    CreateBaseMesh(0);
    m_vertexCount = m_vertices.AppDataLength ();
    Refine(m_indices.m_appData, quality);
    m_faceCount = m_indices.AppDataLength();
    UpdateVAO();
}


void TriangleIcoSphere::CreateBaseMesh(int quality) {
    if (quality == 0)
        CreateOctahedron();
    else
        CreateIcosahedron();
    for (auto& v : m_vertices.m_appData) {
        v.Normalize();
        v *= 0.5f;
    }
}



void TriangleIcoSphere::CreateOctahedron(void) {
    float X = 0.5f;
    float Y = float(sqrt(0.5));
    float Z = 0.5;
    m_vertices.m_appData = { 
        Vector3f{-X,0,-Z}, Vector3f{X,0,-Z}, Vector3f{X,0,Z}, Vector3f{-X,0,Z}, Vector3f{0,-Y,0}, Vector3f{0,Y,0} 
    };
    m_indices.m_appData = { 
        VertexIndices({0,1,5}), VertexIndices({1,2,5}), VertexIndices({2,3,5}), VertexIndices({3,0,5}),
        VertexIndices({0,1,6}), VertexIndices({1,2,6}), VertexIndices({2,3,6}), VertexIndices({3,0,6}) 
    };
}


void TriangleIcoSphere::CreateIcosahedron(void) {
    float X = 0.525731112119133606f;
    float Z = 0.850650808352039932f;
    float N = 0.0f;
    m_vertices.m_appData.Clear();
    m_vertices.m_appData = { 
        Vector3f{-X,+N,+Z}, Vector3f{+X,+N,+Z}, Vector3f{-X,+N,-Z}, Vector3f{+X,+N,-Z},
        Vector3f{+N,+Z,+X}, Vector3f{+N,+Z,-X}, Vector3f{+N,-Z,+X}, Vector3f{+N,-Z,-X},
        Vector3f{+Z, +X, +N}, Vector3f{-Z, +X, +N}, Vector3f{+Z, -X, +N}, Vector3f{-Z, -X, +N} 
    };
    m_indices.m_appData.Clear();
    m_indices.m_appData = {
        VertexIndices({0,4,1}), VertexIndices({0,9,4}), VertexIndices({9,5,4}), VertexIndices({4,5,8}), VertexIndices({4,8,1}),
        VertexIndices({8,10,1}), VertexIndices({8,3,10}), VertexIndices({5,3,8}), VertexIndices({5,2,3}), VertexIndices({2,7,3}),
        VertexIndices({7,10,3}), VertexIndices({7,6,10}), VertexIndices({7,11,6}), VertexIndices({11,0,6}), VertexIndices({0,1,6}),
        VertexIndices({6,1,10}), VertexIndices({9,0,11}), VertexIndices({9,11,2}), VertexIndices({9,2,5}), VertexIndices({7,2,11})
    };
}


void TriangleIcoSphere::SubDivide(SegmentedList<VertexIndices>& faces) {
    SegmentedList<VertexIndices> subFaces;
    Dictionary<VertexKey, GLuint> indexLookup;
#if !(USE_STD || USE_STD_MAP)
    indexLookup.SetComparator(IcoSphere::KeyCmp);
#endif
    for (auto& f : faces) {
        GLuint i0 = AddVertexIndices(indexLookup, f[0], f[1]);
        GLuint i1 = AddVertexIndices(indexLookup, f[1], f[2]);
        GLuint i2 = AddVertexIndices(indexLookup, f[2], f[0]);
        VertexIndices* a;
        a = subFaces.Append();
        *a = { f[0], i0, i2 };
        a = subFaces.Append();
        *a = { f[1], i1, i2 };
        a = subFaces.Append();
        *a = { f[2], i2, i1 };
        a = subFaces.Append();
        *a = { f[0], i1, i2 };
    }
    faces.Clear();
    faces = std::move(subFaces);
}


void TriangleIcoSphere::Refine(SegmentedList<VertexIndices>& faces, int quality) {
    while (quality--)
        SubDivide(faces);
}


// =================================================================================================
// Create an ico sphere based on a shape with rectangular faces

void RectangleIcoSphere::Create(int quality) {
    CreateBaseMesh(0);
    m_vertexCount = m_vertices.AppDataLength ();
    Refine(m_indices.m_appData, quality);
    m_faceCount = m_indices.AppDataLength();
    m_normals = m_vertices;
    UpdateVAO();
}


void RectangleIcoSphere::CreateBaseMesh (int quality = 1) {
    if (quality == 0) 
        CreateCube();
    else
        CreateIcosahedron();
    for (auto& v : m_vertices.m_appData) {
        v.Normalize();
        v *= 0.5f;
    }
}


void RectangleIcoSphere::CreateCube(void) {
    float X = 0.5f;
    float Y = 0.5f;
    float Z = 0.5f;
    m_vertices.m_appData = { 
        Vector3f{-X,-Y,-Z}, Vector3f{+X,-Y,-Z}, Vector3f{+X,+Y,-Z}, Vector3f{-X,+Y,-Z},
        Vector3f{-X,-Y,+Z}, Vector3f{+X,-Y,+Z}, Vector3f{+X,+Y,+Z}, Vector3f{-X,+Y,+Z} 
    };
    m_indices.m_appData = { 
        VertexIndices({0,1,2,3}), VertexIndices({0,4,5,1}), VertexIndices({0,3,7,4}),
        VertexIndices({6,2,1,5}), VertexIndices({6,7,3,2}), VertexIndices({6,5,4,7})
    };
}


void RectangleIcoSphere::CreateIcosahedron(void) {
    float X = 0.5f;
    float Y = 0.5f;
    float Z = 0.5f;
    m_vertices.m_appData.Clear();
    m_vertices.m_appData = {
        // base cube corner vertices
        Vector3f{-X, -Y, -Z}, Vector3f{+X, -Y, -Z}, Vector3f{+X, +Y, -Z}, Vector3f{-X, +Y, -Z},
        Vector3f{-X, +Y, +Z}, Vector3f{-X, -Y, +Z}, Vector3f{+X, +Y, +Z}, Vector3f{+X, -Y, +Z},
        // base cube face center vertices
        Vector3f{0, 0, -Z}, Vector3f{-X, 0, 0}, Vector3f{+X, 0, 0}, Vector3f{0, 0, +Z}, Vector3f{0, +Y, 0}, Vector3f{0, -Y, 0},
        // front face edge center vertices
        Vector3f{0, -Y, -Z}, Vector3f{+X, 0, -Z}, Vector3f{0, +Y, +Z}, Vector3f{-X, 0, -Z},
        // left side face edge center vertices
        Vector3f{-X, +Y, 0}, Vector3f{-X, 0, +Z}, Vector3f{-X, -Y, 0},
        // right side face edge center vertices
        Vector3f{+X, -Y, 0}, Vector3f{+X, 0, +Z}, Vector3f{+X, +Y, 0},
        // front and bottom face rear edge center vertices / back face top and bottom edge center vertices
        Vector3f{0, +Y, +Z}, Vector3f{0, -Y, +Z}
    };
    m_indices.m_appData.Clear();
    m_indices.m_appData = {
        VertexIndices({ 8, 17,  0, 14}), VertexIndices({ 8, 14,  1, 15}), VertexIndices({ 8, 15,  2, 16}), VertexIndices({ 8, 16,  3, 17}),
        VertexIndices({ 9, 17,  3, 18}), VertexIndices({ 9, 18,  4, 19}), VertexIndices({ 9, 19,  5, 20}), VertexIndices({ 9, 20,  0, 17}),
        VertexIndices({10, 15,  1, 21}), VertexIndices({10, 21,  7, 22}), VertexIndices({10, 22,  6, 23}), VertexIndices({10, 23,  2, 15}),
        VertexIndices({11, 22,  7, 25}), VertexIndices({11, 25,  5, 19}), VertexIndices({11, 19,  4, 24}), VertexIndices({11, 24,  6, 22}),
        VertexIndices({12, 16,  2, 23}), VertexIndices({12, 23,  6, 24}), VertexIndices({12, 24,  4, 18}), VertexIndices({12, 18,  3, 16}),
        VertexIndices({13, 14,  0, 18}), VertexIndices({13, 18,  5, 25}), VertexIndices({13, 25,  7, 21}), VertexIndices({13, 21,  1, 14})
    };
}


// Create 4 child quads per existing quad by evenly subdiving each quad.
// To subdivide, compute the center of each side of a quad and the center of the quad.
// Create child quads between the corners and the center of the parent quad.
// Newly created edge center vertices will be shared with child quads of adjacent parent quads,
// So store them in a lookup table that is indexed with the vertex indices of the parent edge.
void RectangleIcoSphere::SubDivide(SegmentedList<VertexIndices>& faces) {
    SegmentedList<VertexIndices> subFaces;
    Dictionary<VertexKey, GLuint> indexLookup;
#if !(USE_STD || USE_STD_MAP)
    indexLookup.SetComparator(IcoSphere::KeyCmp);
#endif
    for (auto& f : faces) {
        GLuint f0 = f[0];
        GLuint f1 = f[1];
        GLuint f2 = f[2];
        GLuint f3 = f[3];
        GLuint i0 = AddVertexIndices(indexLookup, f0, f1);
        GLuint i1 = AddVertexIndices(indexLookup, f1, f2);
        GLuint i2 = AddVertexIndices(indexLookup, f2, f3);
        GLuint i3 = AddVertexIndices(indexLookup, f3, f0);
        GLuint i4 = m_vertexCount++;
        Vector3f v = m_vertices [int(i0)] + m_vertices [int(i1)] + m_vertices [int(i2)] + m_vertices [int(i3)];
        v.Normalize();
        v *= 0.5f;
        m_vertices.m_appData.Append(v);
        VertexIndices* a;
        a = subFaces.Append();
        *a = { f0, i0, i4, i3 };
        a = subFaces.Append();
        *a = { f1, i1, i4, i0 };
        a = subFaces.Append();
        *a = { f2, i2, i4, i1 };
        a = subFaces.Append();
        *a = { f3, i3, i4, i2 };
    }
    faces.Clear();
    faces = std::move(subFaces);
}


void RectangleIcoSphere::Refine(SegmentedList<VertexIndices>& faces, int quality) {
    while (quality--)
        SubDivide(faces);
}

// =================================================================================================
