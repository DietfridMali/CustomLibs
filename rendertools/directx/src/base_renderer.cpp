#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "tristate.h"
#include "base_renderer.h"
#include "base_shaderhandler.h"
#include "shadowmap.h"
#include "commandlist.h"
#include "base_displayhandler.h"
#include "dx12context.h"
#include "gfxapitype.h"

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
    DrawBufferHandler::Setup(m_windowWidth, m_windowHeight);
    int w = m_windowWidth / 15;
    DrawBufferHandler::Setup(m_windowWidth, m_windowHeight);
    m_frameCounter.Setup(::Viewport(m_windowWidth - w, 0, w, int(w * 0.5f / m_aspectRatio)), ColorData::White);
}


bool BaseRenderer::InitGraphics(void) {
#   ifdef _DEBUG
    constexpr bool enableDebugLayer = true;
#   else
    constexpr bool enableDebugLayer = false;
#   endif
    if (not dx12Context.Create(enableDebugLayer)) {
        fprintf(stderr, "Smiley-Battle: Cannot create DX12 device.\n");
        return false;
    }
    if (not commandListHandler.Create(dx12Context.Device())) {
        fprintf(stderr, "Smiley-Battle: Cannot create DX12 command queue.\n");
        return false;
    }
    if (not DescriptorHeapHandler::Instance().Create(dx12Context.Device())) {
        fprintf(stderr, "Smiley-Battle: Cannot create DX12 descriptor heaps.\n");
        return false;
    }
    // Open the command list so displayHandler.Create() and renderer.Create()
    // can record initial state (viewport/scissor, resource barriers).
    if (not commandListHandler.CmdQueue().BeginFrame()) {
        fprintf(stderr, "Smiley-Battle: Cannot begin first DX12 frame.\n");
        return false;
    }
    return true;
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
    m_drawBufferStack.Clear();
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
#if 1
    gfxStates.SetViewport(0, 0, m_windowWidth, m_windowHeight);
#else
    auto* list = commandListHandler.CurrentList();
    if (list) {
        D3D12_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width    = float(m_windowWidth);
        vp.Height   = float(m_windowHeight);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        list->RSSetViewports(1, &vp);
        D3D12_RECT scissor{ 0, 0, m_windowWidth, m_windowHeight };
        list->RSSetScissorRects(1, &scissor);
    }
#endif
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

    if (not (m_screenBuffer and m_screenBuffer->Activate({}))) {
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


void BaseRenderer::Draw3DScene(void) {
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
            m_renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = false /*true*/, .rotation = 0.0f });

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
    m_renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = false /*bFlipVertically*/, .rotation = bRotate ? 90.0f : 0.0f });
    m_renderQuad.Render(nullptr, texture, color);
}


void BaseRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    if (not m_screenIsAvailable)
        return;

    m_frameCounter.Draw(true);
    Stop2DScene();
    m_screenIsAvailable = false;
    if (not m_screenBuffer)
        return;
    m_screenBuffer->Deactivate();

    Set2DRenderStates();

    CommandList* cmdList = commandListHandler.GetCmdList("DrawScreen", true);
    if (not cmdList)
        return;

    cmdList->Open();
    // Ensure the screen RenderTarget color buffer is in PSR state.
    // Stop2DScene() may have run with the list closed (first frame before BeginFrame),
    // in which case the RENDER_TARGET → PSR transition was never recorded.
    if (baseDisplayHandler.CurrentBackBuffer()) {
        baseDisplayHandler.EnableBackBuffer();
        gfxStates.ClearBackBuffer();
    }
#if 0
    Timer t;
    t.SetDuration(500);
    t.Start();
    while (not t.HasExpired())
        ;
#endif
    // Set viewport after the list is open so RSSetViewports is actually recorded.
    SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));

    m_renderTexture.m_handle = m_screenBuffer->BufferHandle(0);
    RenderToViewport(m_screenBuffer->GetAsTexture({}), ColorData::White, bRotate, bFlipVertically);

    // Blit screen RenderTarget to back buffer if not already done (e.g. ProgressIndicator skips DrawScreen).
    // No-op in the normal game loop where DrawScreen() was already called explicitly.
    // Safety: ensure back buffer is in PRESENT state (no-op if DrawScreen already did it).
    if (baseDisplayHandler.CurrentBackBuffer())
        baseDisplayHandler.DisableBackBuffer();
	cmdList->Close();
}


void BaseRenderer::SetViewport(bool flipVertically) noexcept {
    SetViewport(m_viewport, 0, 0, flipVertically);
}


void BaseRenderer::SetViewport(::Viewport viewport, int windowWidth, int windowHeight, bool flipVertically) noexcept {
#ifdef _DEBUG
    flipVertically = false;
#endif
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


CommandList* BaseRenderer::StartOperation(String name) noexcept {
    CommandList* cl = commandListHandler.GetCurrentCmdListObj();
    if (cl) {
        if (cl->IsTemporary())
            ++(cl->m_refCounter);
        return cl;
    }
    if (not m_temporaryList) {
#if LOG_OPERATIONS
        fprintf(stderr, "Opening temp. CL '%s'\n", (const char*)name);
#endif
        m_temporaryList = commandListHandler.GetCmdList(name, true);
        if (not m_temporaryList)
            return nullptr;
        if (not m_temporaryList->Open()) {
            return m_temporaryList = nullptr;
        }
    }
    ++(m_temporaryList->m_refCounter);
    return m_temporaryList;
}


bool BaseRenderer::FinishOperation(void* cl, bool flush) noexcept {
    CommandList* list = static_cast<CommandList*>(cl);
    if (not list)
        return false;
    if (not list->IsTemporary())
        return true;
    if (--(list->m_refCounter) == 0) {
#if LOG_OPERATIONS
        fprintf(stderr, "Closing temp. CL '%s'\n", (const char*)list->GetName());
#endif
        if (flush)
            list->Flush();
        else
            list->Close();
        if (list == m_temporaryList)
            m_temporaryList = nullptr;
    }
    return true;
}

#include "gfxapitype.inl"

// =================================================================================================
