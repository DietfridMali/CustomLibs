#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "tristate.h"
#include "base_renderer.h"
#include "base_shaderhandler.h"
#include "shadowmap.h"
#include "command_queue.h"
#include "base_displayhandler.h"

List<::Viewport> BaseRenderer::viewportStack;

#ifdef _DEBUG
static Texture* testTexture = nullptr;
#endif

// =================================================================================================
// DX12 BaseRenderer
//
// GL-specific calls (glewInit, glClear, glViewport, glGetError …) are removed.
// GfxStates now tracks DX12 render state (see gfxstates.h) instead of calling GL.
// Viewport management: RSSetViewports is called where glViewport was.
// Clear operations: the command list's ClearRenderTargetView / ClearDepthStencilView are used.

bool BaseRenderer::InitDX12(void) noexcept {
    // Nothing to initialize per-renderer in DX12; DX12Context already set up the device.
    return true;
}


void BaseRenderer::Init(int width, int height, float fov, float zNear, float zFar) {
    gfxStates.ReleaseBuffers();
    m_sceneWidth =
    m_windowWidth  = width;
    m_sceneHeight =
    m_windowHeight = height;
    m_viewport     = ::Viewport(0, 0, m_windowWidth, m_windowHeight);
    m_sceneViewport = ::Viewport(m_sceneLeft, m_sceneTop, m_sceneWidth, m_sceneHeight);
    m_fov           = fov;
    m_aspectRatio   = float(m_windowWidth) / float(m_windowHeight);
    SetupDrawBuffers();
    CreateMatrices(m_windowWidth, m_windowHeight, float(m_sceneWidth) / float(m_sceneHeight), fov, zNear, zFar);
    ResetTransformation();
    int w = m_windowWidth / 15;
    DrawBufferHandler::Setup(m_windowWidth, m_windowHeight);
    m_frameCounter.Setup(::Viewport(m_windowWidth - w, 0, w, int(w * 0.5f / m_aspectRatio)), ColorData::White);
}


bool BaseRenderer::CreateScreenBuffer(void) {
    if (m_screenBuffer)
        delete m_screenBuffer;
    if (!(m_screenBuffer = new FBO()))
        return false;
    m_screenBuffer->Create(m_windowWidth, m_windowHeight, 1, { .name = "screen", .colorBufferCount = 1 });
    return true;
}


bool BaseRenderer::Create(int width, int height, float fov, float zNear, float zFar) {
    Init(width, height, fov, zNear, zFar);
    m_viewport = ::Viewport(0, 0, m_windowWidth, m_windowHeight);
    SetupOpenGL();
    m_drawBufferStack.Clear();
    m_renderTexture.HasBuffer() = true;
    m_renderQuad.Setup(BaseQuad::defaultVertices[BaseQuad::voCenter]);
    return true;
}


void BaseRenderer::SetupOpenGL(void) noexcept {
    // Default DX12 render state — same values as the OGL version but stored in GfxStates
    // (no immediate API calls here; state is applied to the PSO on demand).
    gfxStates.ClearColor(ColorData::Invisible);
    gfxStates.ColorMask(true, true, true, true);
    gfxStates.SetDepthWrite(1);
    gfxStates.SetDepthTest(1);
    gfxStates.DepthFunc(GL_LEQUAL);
    gfxStates.SetBlending(0);
    gfxStates.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfxStates.BlendEquation(GL_FUNC_ADD);
    gfxStates.FrontFace(GetWinding());
    gfxStates.SetFaceCulling(1);
    gfxStates.CullFace(GL_BACK);
    gfxStates.SetMultiSample(1);     // no-op in DX12 (MSAA configured at PSO creation)
    gfxStates.SetPolygonOffsetFill(0); // no-op in DX12

    // Set the initial viewport via the DX12 command list.
    auto* list = cmdQueue.List();
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
}


void BaseRenderer::SetDefaultStates(void) noexcept {
    gfxStates.SetDepthWrite(IsColorPass() ? 0 : 1);
    gfxStates.SetDepthTest(1);
    gfxStates.DepthFunc(GL_LEQUAL);
    gfxStates.SetBlending(0);
    gfxStates.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfxStates.FrontFace(GetWinding());
    gfxStates.SetFaceCulling(1);
    gfxStates.CullFace(GL_BACK);
}


void BaseRenderer::StartShadowPass(void) noexcept {
    m_renderPass = RenderPassType::rpShadows;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(1);
    gfxStates.DepthFunc(GL_LESS);
    gfxStates.ColorMask(false, false, false, false);
    gfxStates.SetBlending(0);
}


void BaseRenderer::StartColorPass(void) noexcept {
    m_renderPass = RenderPassType::rpColor;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(0);
    gfxStates.DepthFunc(GL_LEQUAL);
    gfxStates.ColorMask(true, true, true, true);
    gfxStates.SetBlending(0);
}


void BaseRenderer::StartFullPass(void) noexcept {
    m_renderPass = RenderPassType::rpFull;
    gfxStates.SetDepthTest(1);
    gfxStates.SetDepthWrite(1);
    gfxStates.DepthFunc(GL_LEQUAL);
    gfxStates.ColorMask(true, true, true, true);
    gfxStates.SetBlending(0);
}


bool BaseRenderer::Start3DScene(void) {
    SetupOpenGL();
    m_frameCounter.Start();
    FBO* sceneBuffer = GetSceneBuffer();
    if (!(sceneBuffer && sceneBuffer->IsAvailable()))
        return false;
    ResetDrawBuffers(sceneBuffer);
    SetupTransformation();
    SetViewport(m_sceneViewport);
    EnableCamera();
    return true;
}


bool BaseRenderer::Stop3DScene(void) {
    if (!GetSceneBuffer()->IsAvailable())
        return false;
    DisableCamera();
    ResetTransformation();
    return true;
}


bool BaseRenderer::Start2DScene(void) {
    m_frameCounter.Start();
    SetClearColor(m_backgroundColor);
    ResetDrawBuffers(m_screenBuffer, !m_screenIsAvailable);
    m_screenIsAvailable = true;
    ResetTransformation();
    SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));

    if (!(m_screenBuffer && m_screenBuffer->IsAvailable())) {
        // No screen FBO: clear the swap chain back buffer directly.
        auto* list = cmdQueue.List();
        if (list) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = baseDisplayHandler.CurrentRTV();
            float clearColor[4] = {
                m_backgroundColor.R(), m_backgroundColor.G(),
                m_backgroundColor.B(), m_backgroundColor.A()
            };
            list->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        }
    }
    ResetClearColor();
    return true;
}


bool BaseRenderer::Stop2DScene(void) {
    if (!m_screenIsAvailable)
        return false;
    ResetDrawBuffers(nullptr);
    return true;
}


void BaseRenderer::Draw3DScene(void) {
    if (Stop3DScene() && Start2DScene()) {
        gfxStates.DepthFunc(GL_ALWAYS);
        gfxStates.SetFaceCulling(0);
        SetViewport(m_sceneViewport, 0, 0, false);

        Shader* shader;
        if (!UsePostEffectShader())
            shader = nullptr;
        else {
            PushMatrix();
            Translate(0.5, 0.5, 0);
            Scale(1, -1, 1);
            if (!(shader = LoadPostEffectShader()))
                PopMatrix();
        }
        if (shader == nullptr)
            m_renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = true, .rotation = 0.0f });

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
    m_renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = bFlipVertically,
                                       .rotation = bRotate ? 90.0f : 0.0f });
    m_renderQuad.Render(nullptr, texture, color);
}


void BaseRenderer::DrawScreen(bool bRotate, bool bFlipVertically) {
    if (m_screenIsAvailable) {
        m_frameCounter.Draw(true);
        Stop2DScene();
        m_screenIsAvailable = false;
        if (m_screenBuffer) {
            gfxStates.DepthFunc(GL_ALWAYS);
            gfxStates.SetFaceCulling(0);
            SetViewport(::Viewport(0, 0, m_windowWidth, m_windowHeight));

            // Transition back buffer to render target, clear it, then blit the screen FBO.
            auto* list = cmdQueue.List();
            if (list && baseDisplayHandler.CurrentBackBuffer()) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource   = baseDisplayHandler.CurrentBackBuffer();
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &barrier);

                D3D12_CPU_DESCRIPTOR_HANDLE rtv = baseDisplayHandler.CurrentRTV();
                constexpr float black[4] = { 0.f, 0.f, 0.f, 0.f };
                list->ClearRenderTargetView(rtv, black, 0, nullptr);
            }

            m_renderTexture.m_handle = m_screenBuffer->BufferHandle(0);
            RenderToViewport(&m_renderTexture, ColorData::White, bRotate, bFlipVertically);

            // Transition back buffer back to present state.
            if (list && baseDisplayHandler.CurrentBackBuffer()) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource   = baseDisplayHandler.CurrentBackBuffer();
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                list->ResourceBarrier(1, &barrier);
            }
        }
    }
}


void BaseRenderer::SetViewport(bool flipVertically) noexcept {
    SetViewport(m_viewport, 0, 0, flipVertically);
}


void BaseRenderer::SetViewport(::Viewport viewport, int windowWidth, int windowHeight,
                                bool flipViewportVertically, bool flipWindowVertically) noexcept {
    if (windowWidth * windowHeight == 0) {
        if (m_drawBufferInfo.m_fbo) {
            windowWidth  = m_drawBufferInfo.m_fbo->GetWidth(true);
            windowHeight = m_drawBufferInfo.m_fbo->GetHeight(true);
        }
        else {
            windowWidth  = m_windowWidth;
            windowHeight = m_windowHeight;
        }
        // Set the full-surface DX12 viewport.
        auto* list = cmdQueue.List();
        if (list) {
            D3D12_VIEWPORT vp{};
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = 0.0f;
            vp.Width    = float(windowWidth);
            vp.Height   = float(windowHeight);
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            list->RSSetViewports(1, &vp);
            D3D12_RECT scissor{ 0, 0, windowWidth, windowHeight };
            list->RSSetScissorRects(1, &scissor);
        }
    }

    m_viewport = viewport;
    if (flipWindowVertically)
        m_viewport.m_top = windowHeight - m_viewport.m_top - m_viewport.m_height;
    m_viewport.BuildTransformation(windowWidth, windowHeight, flipViewportVertically);
}


void BaseRenderer::PushViewport(void) {
    m_viewport.GetGlViewport(); // stores current dims in m_viewport (DX12: just reads m_viewport)
    viewportStack.Append(m_viewport);
}


void BaseRenderer::PopViewport(void) {
    if (viewportStack.IsEmpty())
        return;
    ::Viewport viewport;
    viewportStack.Pop(viewport);
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
