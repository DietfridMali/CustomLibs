#pragma once

#include "vector.hpp"
#include "matrix.hpp"
#include "glew.h"
#include "shader.h"
#include "projection.h"

// =================================================================================================

class MatrixStack {
public:
    enum class MatrixType {
        mtModelView,
        mtProjection,
        mtProjection2D,
        mtProjection3D,
        mtProjectionFx,
        mtCount
    };

    using enum MatrixType;

    Projection      m_projector;
    Matrix4f        m_transformations[int(mtCount)]; // matrices are row major - let OpenGL transpose them when passing them with glUniformMatrix4fv
    Matrix4f        m_glProjection[3];
    Matrix4f        m_glModelView[3];

    List<Matrix4f>  m_stack;

    inline float ZNear(void) noexcept {
        return m_projector.ZNear();
    }


    inline float ZFar(void) noexcept {
        return m_projector.ZFar();
    }


    inline float FoV(void) noexcept {
        return m_projector.FoV();
    }


    inline float AspectRatio(void) noexcept {
        return m_projector.AspectRatio();
    }


    inline Matrix4f& ModelView(void) noexcept {
        return m_transformations[int(mtModelView)];
    }


    inline Matrix4f& GetProjection(void) noexcept {
        return m_transformations[int(mtProjection)];
    }


    inline Matrix4f& Projection2D(void) noexcept {
        return m_transformations[int(mtProjection2D)];
    }


    inline Matrix4f& Projection3D(void) noexcept {
        return m_transformations[int(mtProjection3D)];
    }


    inline Matrix4f& FxProjection(void) noexcept {
        return m_transformations[int(mtProjectionFx)];
    }


    inline GLfloat* ProjectionMatrix(void) noexcept {
        return (GLfloat*)m_transformations[int(mtProjection)].AsArray();
    }


    Matrix4f* Transformations(void) noexcept {
        return m_transformations;
    }

    Matrix4f& Transformation(MatrixType matrixType) noexcept {
        return m_transformations [int(matrixType)];
    }

    ::Projection& GetProjector(void) noexcept {
        return m_projector;
    }


    void Push(Matrix4f& m) {
        m_stack.Append(m);
    }


    Matrix4f& Pop(Matrix4f& m) {
        m_stack.Pop(m);
        return m;
    }


    void Push(MatrixType matrixType) {
        Push(m_transformations[int(matrixType)]);
    }


    Matrix4f Pop(MatrixType matrixType) {
        return Pop(m_transformations[int(matrixType)]);
    }
};

// =================================================================================================

class RenderMatrices {
public:
    ManagedArray<MatrixStack>   m_matrices;
    int                         m_activeStack;

    static bool                 LegacyMode;

    using MatrixType = MatrixStack::MatrixType;
    using enum MatrixStack::MatrixType;

    RenderMatrices()
        : m_activeStack(0)
    {
        m_matrices.Resize(1);
    }


    void CreateMatrices(int windowWidth, int windowHeight, float aspectRatio, float fov);

    inline int SelectMatrixStack(int i) {
        int current = m_activeStack;
        if (i < m_matrices.Length())
            m_activeStack = i;
        return current;
    }


    MatrixStack* Matrices(int i = -1) {
        return &m_matrices[(i < 0) ? m_activeStack : i];
    }


    int AddMatrices(void) {
        return m_matrices.Append() ? m_matrices.Length() - 1 : -1;
    }


    inline float ZNear(void) noexcept {
        return Matrices().ZNear();
    }


    inline float ZFar(void) noexcept {
        return Matrices().ZFar();
    }


    inline float FoV(void) noexcept {
        return Matrices().FoV();
    }


    inline float AspectRatio(void) noexcept {
        return Matrices().AspectRatio();
    }


    inline Matrix4f& ModelView(void) noexcept {
        return Matrices().ModelView();
    }


    inline Matrix4f& Projection(void) noexcept {
        return Matrices().GetProjection();
    }


    inline Matrix4f& Projection2D(void) noexcept {
        return Matrices().Projection2D();
    }


    inline Matrix4f& Projection3D(void) noexcept {
        return Matrices().Projection3D();
    }


    inline Matrix4f& FxProjection(void) noexcept {
        return Matrices().FxProjection();
    }


    inline GLfloat* ProjectionMatrix(void) noexcept {
        return (GLfloat*)Matrices().GetProjection().AsArray();
    }

    // setup 3D transformation and projection
    void SetupTransformation(void) noexcept;


    void ResetTransformation(void) noexcept;


    template<typename T>
    void SetMatrix(T&& m, MatrixStack::MatrixType matrixType = mtModelView) noexcept {
        if (matrixType == MatrixStack::mtModelView) {
            ModelView() = std::forward<T>(m);
            glMatrixMode(GL_MODELVIEW);
            glLoadMatrixf(std::forward<T>(m).Transpose().AsArray());
        }
        else {
            Projection() = std::forward<T>(m);
            glMatrixMode(GL_PROJECTION);
            glLoadMatrixf(std::forward<T>(m).Transpose().AsArray());
        }
    }


    void PushMatrix(MatrixType matrixType = mtModelView);


    void PopMatrix(MatrixType matrixType = mtModelView);


    bool CheckModelView(void) noexcept;


    bool CheckProjection(void) noexcept;


    Matrix4f& Scale(float xScale, float yScale, float zScale, const char* caller = "") noexcept;


    Matrix4f& Translate(float xTranslate, float yTranslate, float zTranslate, const char* caller = "") noexcept;


    Matrix4f& Rotate(float angle, float xScale, float yScale, float zScale, const char* caller = "") noexcept;


    Matrix4f& Rotate(Matrix4f& r) noexcept;


    Matrix4f& Rotate(Vector3f angles) noexcept;


    inline void Translate(Vector3f v) noexcept {
        Translate(v.X(), v.Y(), v.Z());
    }


    inline Matrix4f& Scale(float scale) noexcept {
        return Scale(scale, scale, scale);
    }


    inline Matrix4f& Scale(Vector3f scale) noexcept {
        return Scale(scale.X(), scale.Y(), scale.Z());
    }


    inline Vector3f Project(Vector3f v) noexcept {
        return static_cast<Vector3f>(Projection() * (ModelView() * static_cast<Vector4f>(v)));
    }

    void PushMatrix(Matrix4f& m) {
        Matrices().Push(m);
    }


    Matrix4f& PopMatrix(Matrix4f& m) {
        Matrices().Pop(m);
        return m;
    }

    void UpdateLegacyMatrices(void) noexcept;
};

// =================================================================================================
