#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "glew.h"
//#include "quad.h"
#include "base_renderer.h"

List<::Viewport> BaseRenderer::viewportStack;

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view transformation
// the renderer enforces window width >= window height, so for portrait screen mode, the window contents
// rendered sideways. That's why BaseRenderer class has m_windowWidth, m_windowHeight and m_aspectRatio
// separate from DisplayHandler.

void BaseRenderer::Init(int width, int height, float fov) {
    m_sceneWidth =
        m_windowWidth = width; // (width > height) ? width : height;
    m_sceneHeight =
        m_windowHeight = height; // (height > width) ? width : height;

    m_aspectRatio = float(m_windowWidth) / float(m_windowHeight); // just for code clarity
    SetupDrawBuffers();
    CreateMatrices(m_windowWidth, m_windowHeight, float(m_sceneWidth) / float(m_sceneHeight), fov);
    ResetTransformation();
    int w = m_windowWidth / 15;
    m_frameCounter.Setup(::Viewport(m_windowWidth - w, 0, w, int(w * 0.5f / m_aspectRatio)), ColorData::White);
}


bool BaseRenderer::CreateScreenBuffer(void) {
    if (not (m_screenBuffer = new FBO()))
        return false;
    m_screenBuffer->Create(m_windowWidth, m_windowHeight, 1, { .name = "screen", .colorBufferCount = 1 }); // FBO for entire screen incl. 2D elements (e.g. UI)
    return true;
}


bool BaseRenderer::Create(int width, int height, float fov) {
    Init(width, height, fov);
    m_viewport = ::Viewport(0, 0, m_windowWidth, m_windowHeight);
    SetupOpenGL();
    m_drawBufferStack.Clear();
    m_renderTexture.HasBuffer() = true;
    m_viewportArea.Setup({ Vector3f{ -0.5f, -0.5f, 0.0f }, Vector3f{ -0.5f, 0.5f, 0.0f }, Vector3f{ 0.5f, 0.5f, 0.0f }, Vector3f{ 0.5f, -0.5f, 0.0f } });
    return true;
}


bool BaseRenderer::InitOpenGL(void) noexcept {
    GLint i = glewInit();
    if (i != GLEW_OK) {
        fprintf(stderr, "Cannot initialize OpenGL\n");
        return false;
    }
    glGetIntegerv(GL_MAJOR_VERSION, &m_glVersion.major);
    glGetIntegerv(GL_MINOR_VERSION, &m_glVersion.minor);
    return true;
}


void BaseRenderer::SetupOpenGL(void) noexcept {
    glClearColor(0, 0, 0, 0);
    glColorMask(1, 1, 1, 1);
    glDepthMask(1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_ALPHA_TEST);
    glFrontFace(GL_CW);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    SetViewport();
}


bool BaseRenderer::Start3DScene(void) {
    m_frameCounter.Start();
    if (not (m_sceneBuffer and m_sceneBuffer->IsAvailable()))
        return false;
    ResetDrawBuffers(m_sceneBuffer);
    SetupTransformation();
    SetupOpenGL();
    SetViewport(::Viewport(0, 0, m_sceneWidth, m_sceneHeight), false);
    EnableCamera();
    return true;
}


bool BaseRenderer::Stop3DScene(void) {
    if (not m_sceneBuffer->IsAvailable())
        return false;
    DisableCamera();
    ResetTransformation();
    return true;
}


bool BaseRenderer::Start2DScene(void) {
    m_frameCounter.Start();
#if 0
    if (not (m_screenBuffer and m_screenBuffer->IsAvailable()))
        return false;
#endif
    SetClearColor(m_backgroundColor);
    ResetDrawBuffers(m_screenBuffer, not m_screenIsAvailable);
    m_screenIsAvailable = true;
    ResetTransformation();
    SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight), false);
    if (not (m_screenBuffer and m_screenBuffer->IsAvailable()))
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ResetClearColor();
    return true;
}


bool BaseRenderer::Stop2DScene(void) {
    if (not m_screenIsAvailable)
        return false;
    ResetDrawBuffers(nullptr);
    return true;
}


void BaseRenderer::Draw3DScene(void) {
    if (Stop3DScene() and Start2DScene()) {
        baseRenderer.PushMatrix();
        baseRenderer.Translate(0.5, 0.5, 0);
        baseRenderer.Scale(1, -1, 1);
        glDepthFunc(GL_ALWAYS);
        glDisable(GL_CULL_FACE);
        SetViewport(::Viewport(m_sceneLeft, m_sceneTop, m_sceneWidth, m_sceneHeight), false);
#if 1
        m_renderTexture.m_handle = m_sceneBuffer->BufferHandle(0);
        m_viewportArea.Render(&m_renderTexture);
#else
        m_viewportArea.Fill(ColorData::Orange);
#endif
        baseRenderer.PopMatrix();
    }
}


void BaseRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    if (m_screenIsAvailable) {
        m_frameCounter.Draw(true);
        Stop2DScene();
        m_screenIsAvailable = false;
        if (m_screenBuffer) {
            glDepthFunc(GL_ALWAYS);
            glDisable(GL_CULL_FACE); // required for vertical flipping because that inverts the buffer's winding
            SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight), false);
            glClear(GL_COLOR_BUFFER_BIT);
            Translate(0.5, 0.5, 0);
            if (bRotate)
                Rotate(90, 0, 0, 1);
            if (bFlipVertically)
                Scale(1, -1, 1);
            m_renderTexture.m_handle = m_screenBuffer->BufferHandle(0);
            m_viewportArea.Render(&m_renderTexture); // bFlipVertically);
        }
    }
}


void BaseRenderer::SetViewport(bool flipVertically) noexcept {
    SetViewport(m_viewport, flipVertically);
}


void BaseRenderer::SetViewport(::Viewport viewport, bool flipVertically) noexcept { //, bool isFBO) {
    if (flipVertically)
        m_viewport = ::Viewport(viewport.m_left, m_windowHeight - viewport.m_top - viewport.m_height, viewport.m_width, viewport.m_height);
    else
        m_viewport = viewport;
    glViewport(m_viewport.m_left, m_viewport.m_top, m_viewport.m_width, m_viewport.m_height);
}


void BaseRenderer::Fill(const RGBAColor& color, float scale) {
    baseRenderer.PushMatrix();
    baseRenderer.Translate(0.5, 0.5, 0.0);
    baseRenderer.Scale(scale, scale, 1);
    m_viewportArea.Fill(color);
    baseRenderer.PopMatrix();
}

void BaseRenderer::ClearGLError(void) noexcept {
#if 0
    while (glGetError() != GL_NO_ERROR)
        ;
#endif
}


bool BaseRenderer::CheckGLError(const char* operation) noexcept {
    return true;
    GLenum glError = glGetError();
    if (not glError)
        return true;
#ifdef _DEBUG
    fprintf(stderr, "OpenGL Error %d (%s)\n", glError, operation);
#endif
    return false;
}

BaseRenderer* baseRendererInstance = nullptr;

// =================================================================================================
