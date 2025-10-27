#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "glew.h"
//#include "quad.h"
#include "tristate.h"
#include "base_renderer.h"

List<::Viewport> BaseRenderer::viewportStack;

#ifdef _DEBUG
static Texture* testTexture = nullptr;
#endif

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
    m_viewport = ::Viewport(0, 0, m_windowWidth, m_windowHeight);
    m_sceneViewport = ::Viewport(m_sceneLeft, m_sceneTop, m_sceneWidth, m_sceneHeight);
    m_aspectRatio = float(m_windowWidth) / float(m_windowHeight); // just for code clarity
    SetupDrawBuffers();
    CreateMatrices(m_windowWidth, m_windowHeight, float(m_sceneWidth) / float(m_sceneHeight), fov);
    ResetTransformation();
    int w = m_windowWidth / 15;
    DrawBufferHandler::Setup(m_windowWidth, m_windowHeight);
    m_frameCounter.Setup(::Viewport(m_windowWidth - w, 0, w, int(w * 0.5f / m_aspectRatio)), ColorData::White);
#if 0//def _DEBUG
    testTexture = textureHandler.GetStandardTexture();
    List<String> fileName = { "resources/textures/ring-512x512.png" };
    if (not testTexture->CreateFromFile(fileName, true)) {
        delete testTexture;
        testTexture = nullptr;
    }
#endif
}


int BaseRenderer::GetDisplayModes(void) {
    int n = SDL_GetNumDisplayModes();
    ManagedArray<SDL_DisplayMode> m(n);
    for (int i = 0; i < n; ++n)
        SDL_GetDisplayMode(0, i, m[i]);

    std::sort(m_displayModes.begin(), m_displayModes.end(),
              [](const SDL_DisplayMode& a, const SDL_DisplayMode& b) { 
                    int64_t areaA = int64_t(a.w) * int64_t(a.h);
                    int64_t areaB = int64_t(b.w) * int64_t(b.h);
                    if (areaA != areaB) 
                        return areaA > areaB;
                    if (a.w != b.w)
                        return a.w > b.w;
                    if (a.refresh_rate != b.refresh_rate) 
                        return a.refresh_rate > b.refresh_rate;
                    return a.format > b.format;
                });

    int64_t ai = 0, aj = 0;
    for (int i = 0; i < n; ++i) {
        aj = ai;
        ai = int64_t(m[i].h) * int64_t(m[j].w);
        if ((i == 0) or (ai != aj))
           m_displayModes.Append(m[i]);
    }
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
    m_renderQuad.Setup(BaseQuad::defaultVertices[BaseQuad::voCenter]);
    return true;
}


bool BaseRenderer::InitOpenGL(void) noexcept {
    GLint i = glewInit();
    if (i != GLEW_OK) {
        fprintf(stderr, "Smiley-Battle: Cannot initialize GLEW.\n");
        return false;
    }
    glGetIntegerv(GL_MAJOR_VERSION, &m_glVersion.major);
    glGetIntegerv(GL_MINOR_VERSION, &m_glVersion.minor);
    return true;
}


void BaseRenderer::SetupOpenGL(void) noexcept {
    openGLStates.ClearColor(ColorData::Invisible);
    openGLStates.ColorMask(1, 1, 1, 1);
    openGLStates.DepthMask(1);
    openGLStates.SetDepthTest(1);
    openGLStates.DepthFunc(GL_LEQUAL);
    openGLStates.SetBlending(0);
#if 1
#   if 1
    openGLStates.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#   else
    openGLStates.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#   endif
#else
    openGLStates.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#endif
    openGLStates.BlendEquation(GL_FUNC_ADD);
    openGLStates.FrontFace(GL_CW);
    openGLStates.SetFaceCulling(1);
    openGLStates.CullFace(GL_BACK);
    openGLStates.openGLStates.SetMultiSample(1);
    openGLStates.SetPolygonOffsetFill(0);
    glViewport(0, 0, m_windowWidth, m_windowHeight);
}


void BaseRenderer::StartDepthPass(void) noexcept {
    m_renderPass = RenderPasses::rpDepth;
    openGLStates.SetDepthTest(1);
    openGLStates.DepthMask(1);                 
    openGLStates.DepthFunc(GL_LESS);                  
    openGLStates.ColorMask(0, 0, 0, 0);
    openGLStates.SetBlending(0);
}


void BaseRenderer::StartColorPass(void) noexcept {
    m_renderPass = RenderPasses::rpColor;
    openGLStates.SetDepthTest(1);
    openGLStates.DepthMask(0);                        
    openGLStates.DepthFunc(GL_LEQUAL);                
    openGLStates.ColorMask(1, 1, 1, 1);
    openGLStates.SetBlending(0);
}


void BaseRenderer::StartFullPass(void) noexcept {
    m_renderPass = RenderPasses::rpColor;
    openGLStates.SetDepthTest(1);
    openGLStates.DepthMask(1);                        
    openGLStates.DepthFunc(GL_LEQUAL);                
    openGLStates.ColorMask(1, 1, 1, 1);
    openGLStates.SetBlending(0);
}


bool BaseRenderer::Start3DScene(void) {
    SetupOpenGL();
    m_frameCounter.Start();
    FBO* sceneBuffer = GetSceneBuffer();
    if (not (sceneBuffer and sceneBuffer->IsAvailable()))
        return false;
    ResetDrawBuffers(sceneBuffer);
    SetupTransformation();
    SetViewport(m_sceneViewport);
    EnableCamera();
    return true;
}


bool BaseRenderer::Stop3DScene(void) {
    if (not GetSceneBuffer()->IsAvailable())
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
    SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));
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
        openGLStates.DepthFunc(GL_ALWAYS);
        openGLStates.SetFaceCulling(0);
        SetViewport(m_sceneViewport, 0, 0, false);
#if 0
        if (GetSceneBuffer()->Enable(true)) {
            RenderToViewport(testTexture, ColorData::White, false, false);
            GetSceneBuffer()->Disable();
        }
#endif
        Shader* shader;
        if (not UsePostEffectShader())
            shader = nullptr;
        else {
            PushMatrix();
            Translate(0.5, 0.5, 0);
            Scale(1, -1, 1);
            if (not (shader = LoadPostEffectShader()))
                PopMatrix();
            }
        if (shader == nullptr) 
            m_renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = true, .rotation = 0.0f });
#if 1
        m_renderTexture.m_handle = GetSceneBuffer()->BufferHandle(0);
        m_renderQuad.Render(shader, &m_renderTexture);
#else
        m_renderQuad.Fill(ColorData::Orange);
#endif
        if (shader != nullptr)
            PopMatrix();
    }
}


void BaseRenderer::RenderToViewport(Texture* texture, RGBAColor color, bool bRotate, bool bFlipVertically) {
#if 0
    Translate(0.5, 0.5, 0);
    if (bRotate)
        Rotate(90, 0, 0, 1);
    if (bFlipVertically)
        Scale(1, -1, 1);
#else
    m_renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = bFlipVertically, .rotation = (bRotate ? 90.0f : 0.0f) });
#endif
#if 1
    m_renderQuad.Render(nullptr, texture, color); // bFlipVertically);
#else
    m_renderQuad.Fill(color); // bFlipVertically);
#endif
}


void BaseRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    if (m_screenIsAvailable) {
        m_frameCounter.Draw(true);
        Stop2DScene();
        m_screenIsAvailable = false;
        if (m_screenBuffer) {
            Tristate<GLenum> depthFunc(GL_NONE, GL_LEQUAL, openGLStates.DepthFunc(GL_ALWAYS));
            openGLStates.SetFaceCulling(0); // required for vertical flipping because that inverts the buffer's winding
            SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));
#if 0
            if (m_screenBuffer->Enable(true)) {
                RenderToViewport(testTexture, ColorData::White, false, false);
                m_screenBuffer->Disable();
            }
#endif
            glClear(GL_COLOR_BUFFER_BIT);
            m_renderTexture.m_handle = m_screenBuffer->BufferHandle(0);
            RenderToViewport(&m_renderTexture, ColorData::White, bRotate, bFlipVertically);
            openGLStates.DepthFunc(depthFunc);
        }
    }
}


void BaseRenderer::SetViewport(bool flipVertically) noexcept {
    SetViewport(m_viewport, 0, 0, flipVertically);
}


// Mapped NDC_local ∈ [-1..1] auf das Ziel-Rect im Full-NDC:
// x' = sx * x + cx,  y' = sy * y + cy
// Column-major Initializer-Reihenfolge (GLM!):
// [ sx  0  0  cx ;  0  sy  0  cy ;  0  0  1   0 ;  0  0  0  1 ]

void BaseRenderer::SetViewport(::Viewport viewport, int windowWidth, int windowHeight, bool flipViewportVertically, bool flipWindowVertically) noexcept { //, bool isFBO) {
#if 1
    if (windowWidth * windowHeight == 0) {
        if (m_drawBufferInfo.m_fbo) {
            windowWidth = m_drawBufferInfo.m_fbo->GetWidth(true);
            windowHeight = m_drawBufferInfo.m_fbo->GetHeight(true);
        }
        else {
            windowWidth = m_windowWidth;
            windowHeight = m_windowHeight;
        }
    }

    m_viewport = viewport;
#if 1
    if (flipWindowVertically)
        m_viewport.m_top = windowHeight - m_viewport.m_top - m_viewport.m_height;
#endif
    m_viewport.BuildTransformation(windowWidth, windowHeight, flipViewportVertically);
#else
    m_viewport = viewport;
    if (flipVertically)
        m_viewport.m_top = windowHeight - m_viewport.m_top - m_viewport.m_height;
    glViewport(m_viewport.m_left, m_viewport.m_top, m_viewport.m_width, m_viewport.m_height);
    m_viewport.BuildTransformation(windowWidth, windowHeight, flipVertically);
#endif
}


void BaseRenderer::Render(Shader* shader, Texture* texture, const RGBAColor& color) {
    baseRenderer.PushMatrix();
    baseRenderer.Translate(0.5f, 0.5f, 0.0f);
    if (shader)
        shader->UpdateMatrices();
    m_renderQuad.Render(shader, texture, color);
    baseRenderer.PopMatrix();
}


void BaseRenderer::Fill(const RGBAColor& color, float scale) {
    baseRenderer.PushMatrix();
    baseRenderer.Translate(0.5f, 0.5f, 0.0f);
    baseRenderer.Scale(scale, scale, 1);
    m_renderQuad.Fill(color);
    baseRenderer.PopMatrix();
}


void BaseRenderer::ClearGLError(void) noexcept {
#ifdef _DEBUG
    while (glGetError() != GL_NO_ERROR)
        ;
#endif
}


bool BaseRenderer::CheckGLError(const char* operation) noexcept {
#ifdef NDEBUG
    return true;
#else
    GLenum glError = glGetError();
    if (not glError)
        return true;
    fprintf(stderr, "Smiley-Battle: OpenGL Error %d (%s)\n", glError, operation);
    ClearGLError();
    return false;
#endif
}

// =================================================================================================
 z