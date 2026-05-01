#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "tristate.h"
#include "base_renderer.h"
#include "base_shaderhandler.h"
#include "base_displayhandler.h"
#include "gfxapitype.h"
#include "shadowmap.h"

List<::Viewport> BaseRenderer::m_viewportStack;

#ifdef _DEBUG
static Texture* testTexture = nullptr;
#endif

#define LOG_OPERATIONS 0

// =================================================================================================
// DX12 BaseRenderer
//
// GL-specific calls (glewInit, glClear, glViewport, glGetError …) are removed.
// GfxStates now tracks DX12 render state (see gfxstates.h) instead of calling GL.
// Viewport management: RSSetViewports is called where glViewport was.
// Clear operations: the command list's ClearRenderTargetView / ClearDepthStencilView are used.

void BaseRenderer::Init(int width, int height, float fov, float zNear, float zFar) {
    gfxStates.ReleaseBuffers();
    m_sceneWidth =
    m_windowWidth = width;
    m_sceneHeight =
    m_windowHeight = height;
    m_viewport = ::Viewport(0, 0, m_windowWidth, m_windowHeight);
    m_sceneViewport = ::Viewport(m_sceneLeft, m_sceneTop, m_sceneWidth, m_sceneHeight);
    m_fov = fov;
    m_aspectRatio = float(m_windowWidth) / float(m_windowHeight);
    CreateMatrices(m_windowWidth, m_windowHeight, float(m_sceneWidth) / float(m_sceneHeight), fov, zNear, zFar);
    ResetTransformation();
    DrawBufferHandler::Setup(WindowWidth(), WindowHeight());
    m_drawBufferStack.Clear();
    int w = m_windowWidth / 15;
    m_frameCounter.Setup(::Viewport(m_windowWidth - w, 0, w, int(w * 0.5f / m_aspectRatio)), ColorData::White);
}


bool BaseRenderer::CreateScreenBuffer(void) {
    if (m_screenBuffer)
        delete m_screenBuffer;
    if (not (m_screenBuffer = new RenderTarget()))
        return false;
    m_screenBuffer->Create(m_windowWidth, m_windowHeight, 1, { .name = "screen", .colorBufferCount = 1 });
    return true;
}


bool BaseRenderer::Create(int width, int height, float fov, float zNear, float zFar) {
    Init(width, height, fov, zNear, zFar);
    m_viewport = ::Viewport(0, 0, m_windowWidth, m_windowHeight);
    SetupGraphics();
    m_renderTexture.Validate();
    m_renderQuad.Setup(BaseQuad::defaultVertices[BaseQuad::voCenter]);
    return true;
}


void BaseRenderer::SetupGraphics(void) noexcept {
    // Default DX12 render state — same values as the OGL version but stored in GfxStates
    // (no immediate API calls here; state is applied to the PSO on demand).
    gfxStates.ClearColor(ColorData::Invisible);
    gfxStates.ColorMask(true, true, true, true);
#if 1
    Set3DRenderStates(1);
#else
    gfxStates.SetDepthWrite(1);
    gfxStates.SetDepthTest(1);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
    gfxStates.SetBlending(0);
    gfxStates.BlendFunc(GfxOperations::BlendFactor::SrcAlpha, GfxOperations::BlendFactor::InvSrcAlpha);
    gfxStates.BlendEquation(GfxOperations::BlendOp::Add);
    gfxStates.FrontFace(GetWinding());
    gfxStates.SetFaceCulling(1);
    gfxStates.CullFace(GfxOperations::FaceCull::Back);
#endif
    // Set the initial viewport via the DX12 command list.
    gfxStates.SetViewport(0, 0, m_windowWidth, m_windowHeight);
}


void BaseRenderer::Set3DRenderStates(int depthWrite) noexcept {
    gfxStates.SetDepthWrite((depthWrite < 0) ? IsColorPass() ? 0 : 1 : depthWrite);
    gfxStates.SetDepthTest(1);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
    gfxStates.SetBlending(0);
    gfxStates.BlendFunc(GfxOperations::BlendFactor::SrcAlpha, GfxOperations::BlendFactor::InvSrcAlpha);
    gfxStates.BlendEquation(GfxOperations::BlendOp::Add);
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


void BaseRenderer::StartShadowPass(void) noexcept {
    m_renderPass = RenderPassType::rpShadows;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(1);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::Less);
    gfxStates.ColorMask(false, false, false, false);
    gfxStates.SetBlending(0);
}


void BaseRenderer::StartColorPass(void) noexcept {
    m_renderPass = RenderPassType::rpColor;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(0);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
    gfxStates.ColorMask(true, true, true, true);
    gfxStates.SetBlending(0);
}


void BaseRenderer::StartFullPass(void) noexcept {
    m_renderPass = RenderPassType::rpFull;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(1);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
    gfxStates.ColorMask(true, true, true, true);
    gfxStates.SetBlending(0);
}


bool BaseRenderer::Start3DScene(void) {
    m_frameCounter.Start();
    SetupGraphics();
    ResetDrawBuffers();
    RenderTarget* sceneBuffer = GetSceneBuffer();
    if (not (sceneBuffer and sceneBuffer->Activate({})))
        return false;
    SetupTransformation();
    SetViewport(m_sceneViewport);
    ActivateCamera();
    return true;
}


bool BaseRenderer::Stop3DScene(void) {
    if (not GetSceneBuffer()->IsAvailable())
        return false;
    GetSceneBuffer()->Deactivate();
    DeactivateCamera();
    ResetTransformation();
    return true;
}


bool BaseRenderer::Start2DScene(void) {
    m_frameCounter.Start();
    ResetDrawBuffers();
    gfxStates.SetClearColor(m_backgroundColor);
    m_screenIsAvailable = true;
    ResetTransformation();
    SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));

    Set2DRenderStates();

    if (not (m_screenBuffer and m_screenBuffer->Activate({ .clear = true }))) {
        baseDisplayHandler.EnableBackBuffer();
        gfxStates.ClearBackBuffer();
    }
    gfxStates.ResetClearColor();
    return true;
}


bool BaseRenderer::Stop2DScene(void) {
    if (not m_screenIsAvailable)
        return false;
    ResetDrawBuffers();
    return true;
}


void BaseRenderer::Draw3DScene(bool flipVertically) {
    if (Stop3DScene() and Start2DScene()) {
        Set2DRenderStates();
        SetViewport(m_sceneViewport, 0, 0, false);

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
            m_renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = flipVertically, .rotation = 0.0f });

        static bool renderScene = true;
        if (renderScene) {
            m_renderTexture.m_handle = GetSceneBuffer()->BufferHandle(0);
            m_renderQuad.Render(shader, { &m_renderTexture });
        }
        if (shader != nullptr)
            PopMatrix();
    }
}


void BaseRenderer::RenderToViewport(Texture* texture, RGBAColor color, bool bRotate, bool bFlipVertically) {
    m_renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = bFlipVertically, .rotation = bRotate ? 90.0f : 0.0f });
    m_renderQuad.Render(nullptr, texture, color);
}


void BaseRenderer::SetViewport(bool flipVertically) noexcept {
    SetViewport(m_viewport, 0, 0, flipVertically);
}


void BaseRenderer::SetViewport(::Viewport viewport, int windowWidth, int windowHeight, bool flipVertically) noexcept {
#ifdef _DEBUG
    flipVertically = false;
#endif
    if (windowWidth * windowHeight == 0) {
        RenderTarget* activeBuffer = GetActiveBuffer();
        if (activeBuffer) {
            windowWidth = activeBuffer->GetWidth(true);
            windowHeight = activeBuffer->GetHeight(true);
        }
        else {
            windowWidth = m_windowWidth;
            windowHeight = m_windowHeight;
        }
    }
    gfxStates.SetViewport(0, 0, windowWidth, windowHeight);

    m_viewport = viewport;
    if (flipVertically)
        m_viewport.m_top = windowHeight - m_viewport.m_top - m_viewport.m_height;
    m_viewport.BuildTransformation(windowWidth, windowHeight, flipVertically);
}


void BaseRenderer::PushViewport(void) {
    m_viewport.GetGfxViewport();
    m_viewportStack.Append(m_viewport);
}


void BaseRenderer::PopViewport(void) {
    if (m_viewportStack.IsEmpty())
        return;
    ::Viewport viewport;
    m_viewportStack.Pop(viewport);
    if ((viewport.Width() > WindowWidth()) || (viewport.Height() > WindowHeight()))
        return;
    SetViewport(viewport, viewport.WindowWidth(), viewport.WindowHeight(), viewport.FlipVertically());
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

// =================================================================================================
