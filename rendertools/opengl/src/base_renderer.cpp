#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "glew.h"
//#include "quad.h"
#include "base_renderer.h"
#include "base_shaderhandler.h"
#include "shadowmap.h"

List<::Viewport> BaseRenderer::m_viewportStack;

#ifdef _DEBUG
static Texture* testTexture = nullptr;
#endif

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view transformation
// the renderer enforces window width >= window height, so for portrait screen mode, the window contents
// rendered sideways. That's why BaseRenderer class has m_windowWidth, m_windowHeight and m_aspectRatio
// separate from DisplayHandler.

void BaseRenderer::Init(int width, int height, float fov, float zNear, float zFar) {
    gfxStates.ReleaseBuffers();
    m_sceneWidth =
    m_windowWidth = width; // (width > height) ? width : height;
    m_sceneHeight =
    m_windowHeight = height; // (height > width) ? width : height;
    m_viewport = ::Viewport(0, 0, m_windowWidth, m_windowHeight);
    m_sceneViewport = ::Viewport(m_sceneLeft, m_sceneTop, m_sceneWidth, m_sceneHeight);
    m_fov = fov;
    m_aspectRatio = float(m_windowWidth) / float(m_windowHeight); // just for code clarity
    CreateMatrices(m_windowWidth, m_windowHeight, float(m_sceneWidth) / float(m_sceneHeight), fov, zNear, zFar);
    ResetTransformation();
    DrawBufferHandler::Setup(m_windowWidth, m_windowHeight);
    int w = m_windowWidth / 15;
    m_frameCounter.Setup(::Viewport(m_windowWidth - w, 0, w, int(w * 0.5f / m_aspectRatio)), ColorData::White);
#if 0//def _DEBUG
    List<String> fileName = { "connect.png" };
    testTexture = textureHandler.GetStandardTexture(fileName[0]);
    if (not testTexture->CreateFromFile(String("assets/textures/"), fileName, {})) {
        delete testTexture;
        testTexture = nullptr;
    }
#endif
}


bool BaseRenderer::CreateScreenBuffer(void) {
    if (m_screenBuffer)
        delete m_screenBuffer;
    if (not (m_screenBuffer = new RenderTarget()))
        return false;
    m_screenBuffer->Create(m_windowWidth, m_windowHeight, 1, { .name = "screen", .colorBufferCount = 1 }); // RenderTarget for entire screen incl. 2D elements (e.g. UI)
    return true;
}


bool BaseRenderer::Create(int width, int height, float fov, float zNear, float zFar) {
    Init(width, height, fov, zNear, zFar);
    m_viewport = ::Viewport(0, 0, m_windowWidth, m_windowHeight);
    SetupGraphics();
    m_drawBufferStack.Clear();
    m_renderTexture.Validate();
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


void BaseRenderer::Set3DRenderStates(int depthWrite) noexcept {
    gfxStates.SetDepthWrite((depthWrite < 0) ? IsColorPass() ? 0 : 1 : depthWrite);
    gfxStates.SetDepthTest(1);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
    gfxStates.SetBlending(0);
    gfxStates.BlendFunc(GfxOperations::BlendFactor::SrcAlpha, GfxOperations::BlendFactor::InvSrcAlpha);
    gfxStates.FrontFace(GetWinding());
    gfxStates.SetFaceCulling(1);
    gfxStates.CullFace(GfxOperations::FaceCull::Back);
}


void BaseRenderer::Set2DRenderStates(int blending) noexcept {
    gfxStates.SetDepthTest(0);
    gfxStates.SetDepthWrite(0);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::Always);
    gfxStates.SetFaceCulling(0);
    gfxStates.SetBlending(blending);
}


void BaseRenderer::SetupGraphics(void) noexcept {
    Set3DRenderStates();
    gfxStates.SetDepthWrite(1);
    gfxStates.ClearColor(ColorData::Invisible);
    glClearDepth(1.0);
    gfxStates.ColorMask(1, 1, 1, 1);
#if 1
#   if 1
    gfxStates.BlendFunc(GfxOperations::BlendFactor::SrcAlpha, GfxOperations::BlendFactor::InvSrcAlpha);
#   else
    gfxStates.BlendFunc(GfxOperations::BlendFactor::One, GfxOperations::BlendFactor::InvSrcAlpha);
#   endif
#else
    gfxStates.BlendFuncSeparate(GfxOperations::BlendFactor::One, GfxOperations::BlendFactor::InvSrcAlpha, GfxOperations::BlendFactor::One, GfxOperations::BlendFactor::InvSrcAlpha);
#endif
    gfxStates.BlendEquation(GfxOperations::BlendOp::Add);
    gfxStates.SetMultiSample(1);
    gfxStates.SetPolygonOffsetFill(0);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glViewport(0, 0, m_windowWidth, m_windowHeight);
}


void BaseRenderer::StartShadowPass(void) noexcept {
    m_renderPass = RenderPassType::rpShadows;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(1);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::Less);
    gfxStates.ColorMask(0, 0, 0, 0);
    //gfxStates.ColorMask(1, 1, 1, 1);
    gfxStates.SetBlending(0);
}


void BaseRenderer::StartColorPass(void) noexcept {
    m_renderPass = RenderPassType::rpColor;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(0);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
    gfxStates.ColorMask(1, 1, 1, 1);
    gfxStates.SetBlending(0);
}


void BaseRenderer::StartFullPass(void) noexcept {
    m_renderPass = RenderPassType::rpFull;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(1);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
    gfxStates.ColorMask(1, 1, 1, 1);
    gfxStates.SetBlending(0);
}


bool BaseRenderer::Start3DScene(void) {
    SetupGraphics();
    ResetDrawBuffers();
    m_frameCounter.Start();
    RenderTarget* sceneBuffer = GetSceneBuffer();
    if (not (sceneBuffer and sceneBuffer->Enable()))
        return false;
    SetupTransformation();
	//3D render is always full window; to put it in a window, render the scene buffer in a window in Draw3DScene()
    //SetViewport(m_sceneViewport);
    EnableCamera();
    return true;
}


bool BaseRenderer::Stop3DScene(void) {
    if (not GetSceneBuffer()->IsAvailable())
        return false;
    GetSceneBuffer()->Disable();
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
    gfxStates.SetClearColor(m_backgroundColor);
    ResetDrawBuffers();
    m_screenIsAvailable = true;
    ResetTransformation();
    SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));
    if (not (m_screenBuffer and m_screenBuffer->Enable())) {
        gfxStates.ClearColorBuffers();
        gfxStates.ClearDepthBuffer();
    }
    SetViewport(m_sceneViewport, 0, 0, false);
    gfxStates.SetDepthWrite(0);
    gfxStates.SetDepthTest(0);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::Always);
    gfxStates.SetFaceCulling(0);
    return true;
}


bool BaseRenderer::Stop2DScene(void) {
    if (not m_screenIsAvailable)
        return false;
    ResetDrawBuffers();
    return true;
}


void BaseRenderer::Draw3DScene(void) {
    if (Stop3DScene() and Start2DScene()) {
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
        static bool renderScene = true;

#define TEST_RENDER 1

#if TEST_RENDER
        if (renderScene)
#endif
        {
#if 0
            Fill(ColorData::Orange);
#else
            m_renderTexture.m_handle = GetSceneBuffer()->BufferHandle(0);
            m_renderQuad.Render(shader, { &m_renderTexture });
#endif
        }
#if TEST_RENDER
        else { // test render shadow map
            Texture* t = shadowMap.ShadowTexture();
            if (t) {
                Translate(0.5, 0.5, 0);
                m_renderQuad.Render(baseShaderHandler.SetupShader("depthRenderer"), { t });
                Translate(-0.5, -0.5, 0);
            }
        }
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
            gfxStates.DepthFunc(GfxOperations::CompareFunc::Always);
            gfxStates.SetFaceCulling(0); // required for vertical flipping because that inverts the buffer's winding
            //SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));
#if 0
            if (m_screenBuffer->Enable()) {
                RenderToViewport(testTexture, ColorData::White, false, false);
                m_screenBuffer->Disable();
            }
#endif
            gfxStates.ClearColorBuffers();
            RenderToViewport(m_screenBuffer->GetAsTexture({}), ColorData::White, bRotate, bFlipVertically);
        }
    }
}


void BaseRenderer::SetViewport(bool flipVertically) noexcept {
    SetViewport(m_viewport, 0, 0, flipVertically);
}


// Mapped NDC_local in [-1..1] auf das Ziel-Rect im Full-NDC:
// x' = sx * x + cx,  y' = sy * y + cy
// Column-major Initializer-Reihenfolge (GLM!):
// [ sx  0  0  cx ;  0  sy  0  cy ;  0  0  1   0 ;  0  0  0  1 ]

// Generelles zum Viewport - Handling.Dieses ist zweiteilig : Setzen des gfx - Viewports(OpenGL / DX) für korrekte Projektion und setzen
// aktueller Renderbereiche(app - eigene Viewportmatrix, die in die Shader geht).Jede Aktivierung eines render targets muss auch den
// gfx - Viewport setzen.Das macht inzwischen RenderTarget::Enable, das den vorigen Viewport speichert(BaseRenderer::PushViewport -
// speichert auch den gfx - Viewport).BaseRenderer::SetViewport erhält deshalb bei jedem RT - Enable dessen Pufferdimensionen als windowWidth
// und windowHeight, dann wird der gfx - Viewport entspr.gesetzt.Um innerhalb eines RTs einen Viewport zu setzen, werden für windowWidth
// und windowHeight 0 übergeben, dann wird der gfx - Viewport nicht verändert.Disable stellt den vorhergehenden Viewport(app + gfx) wieder
// her.In OpenGL ist das ein bleibender Status und beeinflusst die 2D - Projektion.

void BaseRenderer::SetViewport(::Viewport viewport, int windowWidth, int windowHeight, bool flipVertically) noexcept { //, bool isRenderTarget) {
    if (windowWidth * windowHeight == 0) {
#if 0
        if (m_parentBuffer) {
            windowWidth = m_parentBuffer->GetWidth(true);
            windowHeight = m_parentBuffer->GetHeight(true);
        }
        else 
#endif
        if (m_activeBuffer) {
            windowWidth = m_activeBuffer->GetWidth(true);
            windowHeight = m_activeBuffer->GetHeight(true);
        }
        else {
            windowWidth = m_windowWidth;
            windowHeight = m_windowHeight;
        }
    }
    gfxStates.SetViewport(0, 0, windowWidth, windowHeight);
    m_viewport = viewport;
#if 1
    if (flipVertically)
        m_viewport.m_top = windowHeight - m_viewport.m_top - m_viewport.m_height;
#endif
#ifdef _DEBUG
    m_viewport.GetGfxViewport();
#endif
    m_viewport.BuildTransformation(windowWidth, windowHeight, flipVertically);
    //glViewport(m_viewport.m_left, m_viewport.m_top, m_viewport.m_width, m_viewport.m_height);
}


void BaseRenderer::Render(Shader* shader, std::span<Texture* const> textures, const RGBAColor& color) {
    baseRenderer.PushMatrix();
    baseRenderer.Translate(0.5f, 0.5f, 0.0f);
    if (shader)
        shader->UpdateMatrices();
    m_renderQuad.Render(shader, textures, color);
    baseRenderer.PopMatrix();
}


void BaseRenderer::Fill(const RGBAColor& color, float scale) {
    baseRenderer.PushMatrix();
    baseRenderer.Translate(0.5f, 0.5f, 0.0f);
    baseRenderer.Scale(scale, scale, 1);
    m_renderQuad.Fill(color);
    baseRenderer.PopMatrix();
}


void BaseRenderer::PopViewport(void) {
    if (m_viewportStack.IsEmpty())
        return;
    ::Viewport viewport;
    m_viewportStack.Pop(viewport);
    if ((viewport.Width() > WindowWidth()) or (viewport.Height() > WindowHeight()))
        return;
    SetViewport(viewport, viewport.WindowWidth(), viewport.WindowHeight(), viewport.FlipVertically());
#if 1
    m_viewport.SetGfxViewport();
#endif
}



void BaseRenderer::ClearGfxError(void) noexcept {
#ifdef _DEBUG
    while (glGetError() != GL_NO_ERROR)
        ;
#endif
}


bool BaseRenderer::CheckGfxError(const char* operation) noexcept {
#ifdef NDEBUG
    return true;
#else
    GLenum glError = glGetError();
    if (not glError)
        return true;
    fprintf(stderr, "Smiley-Battle: Graphics Error %d (%s)\n", glError, operation);
    ClearGfxError();
    return false;
#endif
}

#include "gfxapitype.inl"

// =================================================================================================
