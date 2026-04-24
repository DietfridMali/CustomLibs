
#include "rendermatrices.h"

#define DEBUG_MATRICES 0

#if DEBUG_MATRICES
bool RenderMatrices::LegacyMode = false;
#else
bool RenderMatrices::LegacyMode = false;
#endif

#ifdef _DEBUG
#   define  LOG_MATRIX_OPERATIONS 0
#else
#   define  LOG_MATRIX_OPERATIONS 0
#endif

using enum MatrixStack::MatrixType;

// =================================================================================================

void RenderMatrices::CreateMatrices(int windowWidth, int windowHeight, float aspectRatio, float fov, float zNear, float zFar) {
    ModelView() = Matrix4f::IDENTITY;
    Projector projector = GetProjector();
    projector.Setup(aspectRatio, fov, zNear, zFar);
    Projection2D() = projector.ComputeOrthoProjection(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
    Projection2D().AsArray();
    Projection3D() = projector.Compute3DProjection();
    Projection3D().AsArray();
}


void RenderMatrices::SetupTransformation(void) noexcept {
    ModelView() = Matrix4f::IDENTITY;
    Projection() = Projection3D();
}


void RenderMatrices::ResetTransformation(void) noexcept {
    ModelView() = Matrix4f::IDENTITY;
    Projection() = Projection2D();
}


bool RenderMatrices::CheckModelView(void) noexcept {
    return true;
}


bool RenderMatrices::CheckProjection(void) noexcept {
    return true;
}


Matrix4f& RenderMatrices::Scale(float xScale, float yScale, float zScale, const char* caller) noexcept {
    ModelView().Scale(xScale, yScale, zScale);
    return ModelView();
}


Matrix4f& RenderMatrices::Translate(float xTranslate, float yTranslate, float zTranslate, const char* caller) noexcept {
    ModelView().Translate(xTranslate, yTranslate, zTranslate);
    return ModelView();
}


Matrix4f& RenderMatrices::Rotate(float angle, float xScale, float yScale, float zScale, const char* caller) noexcept {
    ModelView().Rotate(angle, xScale, yScale, zScale);
    return ModelView();
}


Matrix4f& RenderMatrices::Rotate(Matrix4f& r) noexcept {
    ModelView().Rotate(r);
    return ModelView();
}


Matrix4f& RenderMatrices::Rotate(Vector3f angles) noexcept {
#if USE_GLM
    Matrix4f r = Matrix4f::Rotation(angles);
#else
    Matrix4f r = Matrix4f::Rotation(angles, ModelView().IsColMajor());
#endif
    return Rotate(r);
}


void RenderMatrices::PushMatrix(MatrixType matrixType) {
    Matrices()->Push(matrixType);
}


void RenderMatrices::PopMatrix(MatrixType matrixType) {
    Matrices()->Pop(matrixType);
}


void RenderMatrices::UpdateLegacyMatrices(void) noexcept {
}

// =================================================================================================
