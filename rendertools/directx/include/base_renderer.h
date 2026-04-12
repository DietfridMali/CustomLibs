#pragma once

#include <math.h>
#include <utility>
#include <stdlib.h>

#include "framework.h"
#include "std_defines.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "matrix.hpp"
#include "projector.h"
#include "rendermatrices.h"
#include "viewport.h"
#include "fbo.h"
#include "drawbufferhandler.h"
#include "gfxstates.h"
#include "framecounter.h"

// =================================================================================================
// DX12 BaseRenderer
//
// Manages the 3D scene FBOs, coordinate transforms, viewports, and the render loop.
// "OpenGL" terminology is preserved in function names for source compatibility with the game layer
// (SetupOpenGL → SetupDX12 internally, but callers still see SetupOpenGL for now).

class BaseRenderer
    : public DrawBufferHandler
    , public RenderMatrices
    , public PolymorphSingleton<BaseRenderer>
{
public:
    enum class RenderPassType {
        rpShadows,
        rpColor,
        rpFull
    };

protected:
    FBO*                    m_screenBuffer;
    FBO*                    m_sceneBuffer;
    FBO*                    m_skyBuffer;
    Texture                 m_renderTexture;
    bool                    m_screenIsAvailable;

    Viewport                m_viewport;
    Viewport                m_sceneViewport;
    Vector2f                m_ndcScale;
    Vector2f                m_ndcBias;
    BaseQuad                m_renderQuad;

    int                     m_windowWidth;
    int                     m_windowHeight;
    int                     m_sceneLeft;
    int                     m_sceneTop;
    int                     m_sceneWidth;
    int                     m_sceneHeight;
    float                   m_fov;
    float                   m_aspectRatio;

    RGBAColor               m_backgroundColor;
    RenderPassType          m_renderPass;
    MovingFrameCounter      m_frameCounter;

    static List<::Viewport> viewportStack;

public:
#ifdef _DEBUG
    bool                    m_xchgSkyAndSceneBuffer{ false };
#endif

    BaseRenderer()
        : m_screenBuffer(nullptr)
        , m_sceneBuffer(nullptr)
        , m_skyBuffer(nullptr)
        , m_windowWidth(0)
        , m_windowHeight(0)
        , m_sceneWidth(0)
        , m_sceneHeight(0)
        , m_sceneLeft(0)
        , m_sceneTop(0)
        , m_fov(0.0f)
        , m_aspectRatio(1.0f)
        , m_renderPass(RenderPassType::rpColor)
        , m_ndcScale(Vector2f::ONE)
        , m_ndcBias(Vector2f::ONE)
        , m_backgroundColor(ColorData::Black)
        , m_screenIsAvailable(false)
    {
        _instance = this;
    }

    static BaseRenderer& Instance(void) {
        return dynamic_cast<BaseRenderer&>(PolymorphSingleton::Instance());
    }

    // DX12 one-time init (no equivalent of glewInit).
    bool InitDX12(void) noexcept;

    virtual void Init(int width, int height, float fov, float zNear, float zFar);

    virtual bool Create(int width = 1920, int height = 1080, float fov = 45.0f,
                        float zNear = 0.1f, float zFar = 100.0f);

    bool CreateScreenBuffer(void);

    virtual FBO* GetSceneBuffer(void) noexcept {
#ifdef _DEBUG
        return m_xchgSkyAndSceneBuffer ? m_skyBuffer : m_sceneBuffer;
#else
        return m_sceneBuffer;
#endif
    }

    FBO* GetSkyBuffer(void) noexcept {
#ifdef _DEBUG
        return m_xchgSkyAndSceneBuffer ? m_sceneBuffer : m_skyBuffer;
#else
        return m_skyBuffer;
#endif
    }

    virtual void ActivateSceneViewport(void) noexcept {
        SetViewport(::Viewport(0, 0, m_sceneWidth, m_sceneHeight));
    }

    // Sets default DX12 pipeline state (depth, blend, rasterizer …).
    // Mirrors SetupOpenGL from the OGL version — same call sites, DX12 internals.
    void SetupOpenGL(void) noexcept;

    void SetDefaultStates(void) noexcept;

    inline void SetRenderPass(RenderPassType renderPass) noexcept { m_renderPass = renderPass; }

    void StartShadowPass(void) noexcept;
    void StartColorPass(void)  noexcept;
    void StartFullPass(void)   noexcept;

    inline bool IsShadowPass(void) noexcept { return RenderPass() == RenderPassType::rpShadows; }
    inline bool IsColorPass(void)  noexcept { return RenderPass() == RenderPassType::rpColor; }
    inline bool IsFullPass(void)   noexcept { return RenderPass() == RenderPassType::rpFull; }

    inline void StartRenderPass(RenderPassType pass) noexcept {
        if (pass == RenderPassType::rpShadows)      StartShadowPass();
        else if (pass == RenderPassType::rpColor)   StartColorPass();
        else                                         StartFullPass();
    }

    RenderPassType RenderPass(void) noexcept { return m_renderPass; }

    virtual bool Start3DScene(void);
    virtual bool Stop3DScene(void);
    virtual bool Start2DScene(void);
    virtual bool Stop2DScene(void);

    virtual bool UsePostEffectShader(void)    { return false; }
    virtual Shader* LoadPostEffectShader(void){ return nullptr; }

    inline void SetSceneViewport(Viewport viewport) noexcept { m_sceneViewport = viewport; }
    inline ::Viewport GetSceneViewport(void)        noexcept { return m_sceneViewport; }

    virtual void Draw3DScene(void);
    virtual void RenderToViewport(Texture* texture, RGBAColor color, bool bRotate, bool bFlipVertically);
    virtual void DrawScreen(bool bRotate, bool bFlipVertically);

    virtual bool EnableCamera(void)  { return false; }
    virtual bool DisableCamera(void) { return false; }

    inline FBO* ScreenBuffer(void) noexcept { return m_screenBuffer; }
    inline int WindowWidth(void)   noexcept { return m_windowWidth; }
    inline int WindowHeight(void)  noexcept { return m_windowHeight; }
    inline int SceneWidth(void)    noexcept { return m_sceneWidth; }
    inline int SceneHeight(void)   noexcept { return m_sceneHeight; }
    inline int SceneLeft(void)     noexcept { return m_sceneLeft; }
    inline int SceneTop(void)      noexcept { return m_sceneTop; }
    inline float FOV(void)         noexcept { return m_fov; }
    inline float AspectRatio(void) noexcept { return m_aspectRatio; }

    inline Matrix4f& ViewportTransformation(void) noexcept { return m_viewport.Transformation(); }
    inline Vector2f& NDCScale(void)               noexcept { return m_ndcScale; }
    inline Vector2f& NDCBias(void)                noexcept { return m_ndcBias; }
    inline MovingFrameCounter& FrameCounter(void) noexcept { return m_frameCounter; }

    template <typename T>
    inline void SetBackgroundColor(T&& backgroundColor) {
        m_backgroundColor = std::forward<T>(backgroundColor);
    }

    // Sets the clear color for the next frame; no immediate API call in DX12.
    inline void SetClearColor(const RGBAColor& color) noexcept {
        gfxStates.ClearColor(color);
    }
    inline void SetClearColor(RGBAColor&& color) noexcept {
        SetClearColor(static_cast<const RGBAColor&>(color));
    }
    inline void ResetClearColor(void) noexcept {
        gfxStates.ClearColor(RGBAColor{ 0.f, 0.f, 0.f, 0.f });
    }

    inline BaseQuad& RenderQuad(void)     noexcept { return m_renderQuad; }
    inline ::Viewport& GetViewport(void)  noexcept { return m_viewport; }

    void SetViewport(bool flipVertically = false) noexcept;
    void SetViewport(::Viewport viewport, int windowWidth = 0, int windowHeight = 0,
                     bool flipViewportVertically = false, bool flipWindowVertically = false) noexcept;

    void PushViewport(void);
    void PopViewport(void);

    inline TexCoord ViewportSize(void) noexcept {
        return TexCoord(float(m_viewport.Width()), float(m_viewport.Height()));
    }
    inline TexCoord TexelSize(void) noexcept {
        return TexCoord(1.0f / float(m_viewport.Width()), 1.0f / float(m_viewport.Height()));
    }

    void Render(Shader* shader, std::span<Texture* const> textures = {}, const RGBAColor& color = ColorData::White);

    inline void Render(Shader* shader, std::initializer_list<Texture*> textures,
                       const RGBAColor& color = ColorData::White) {
        Render(shader, std::span<Texture* const>(textures.begin(), textures.size()), color);
    }
    inline void Render(Shader* shader, Texture* texture, const RGBAColor& color) {
        Render(shader, texture ? std::span<Texture* const>(&texture, 1) : std::span<Texture* const>{}, color);
    }

    void Fill(const RGBAColor& color, float scale = 1.0f);
    void Fill(RGBAColor&& color, float scale = 1.0f) {
        Fill(static_cast<const RGBAColor&>(color), scale);
    }
    template <typename T>
    inline void Fill(T&& color, float alpha, float scale = 1.0f) {
        Fill(RGBAColor(std::forward<T>(color), alpha), scale);
    }

    inline void ShowFps(bool showFps)  noexcept { m_frameCounter.ShowFps(showFps); }
    inline void ToggleFps(void)        noexcept { m_frameCounter.Toggle(); }
    inline float GetFps(void)          noexcept { return m_frameCounter.GetFps(); }

    // Winding helpers — return GL enum values (defined in gfxstates.h compat section).
    inline GLenum GetWinding(bool reverse = false)  noexcept { return reverse ? GLenum(GL_CCW) : GLenum(GL_CW); }
    inline GLenum GetFrontFace(bool reverse = false) noexcept { return reverse ? GLenum(GL_FRONT) : GLenum(GL_BACK); }

    // DX12: no per-frame GL error checking. These are kept as no-ops for source compatibility.
    static void ClearGLError(void)                    noexcept {}
    static bool CheckGLError(const char* = "") noexcept { return true; }
};

using RenderPassType = BaseRenderer::RenderPassType;

#define baseRenderer BaseRenderer::Instance()

// =================================================================================================
