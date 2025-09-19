#pragma once
#pragma once

#include <utility>

#include <stdlib.h>
#include <math.h>
#include "singletonbase.hpp"
#include "array.hpp"
#include "matrix.hpp"
#include "projection.h"
#include "rendermatrices.h"
#include "viewport.h"
#include "fbo.h"
#include "drawbufferhandler.h"
#include "opengl_states.h"
#include "framecounter.h"

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view matrix

class BaseRenderer
    : public DrawBufferHandler
    , public RenderMatrices
    , public PolymorphSingleton<BaseRenderer>
{
public:
    struct GLVersion {
        GLint major{ 0 };
        GLint minor{ 0 };
    };

    enum class RenderPasses {
        rpDepth,
        rpColor,
        rpFull
    };

protected:
    FBO*                    m_screenBuffer;
    FBO*                    m_sceneBuffer;
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
    float                   m_aspectRatio;

    RGBAColor               m_backgroundColor;

    RenderPasses            m_renderPass;

    MovingFrameCounter      m_frameCounter;

    static List<::Viewport> viewportStack;
public:
    GLVersion               m_glVersion;

    BaseRenderer()
        : m_screenBuffer(nullptr)
        , m_sceneBuffer(nullptr)
        , m_windowWidth(0)
        , m_windowHeight(0)
        , m_sceneWidth(0)
        , m_sceneHeight(0)
        , m_sceneLeft(0)
        , m_sceneTop(0)
        , m_aspectRatio(1.0f)
        , m_renderPass(RenderPasses::rpColor)
        , m_ndcScale(Vector2f::ONE)
        , m_ndcBias(Vector2f::ONE)
        , m_backgroundColor(ColorData::Black)
        , m_screenIsAvailable(false)
    {
        //_instance = this;
    }

    static BaseRenderer& Instance(void) { return dynamic_cast<BaseRenderer&>(PolymorphSingleton::Instance()); }

    bool InitOpenGL(void) noexcept;

    virtual void Init(int width, int height, float fov);

    virtual bool Create(int width = 1920, int height = 1080, float fov = 45);

    bool CreateScreenBuffer(void);

    void SetupOpenGL(void) noexcept;

    inline void SetRenderPass(RenderPasses renderPass) noexcept { m_renderPass = renderPass; }

    void StartDepthPass(void) noexcept;

    void StartColorPass(void) noexcept;

    void StartFullPass(void) noexcept;

    inline bool DepthPass(void) noexcept { return RenderPass() == RenderPasses::rpDepth; }

    inline bool ColorPass(void) noexcept { return RenderPass() == RenderPasses::rpColor; }

    inline bool FullPass(void) noexcept { return RenderPass() == RenderPasses::rpFull; }

    inline void StartRenderPass(RenderPasses pass) noexcept {
        if (pass == RenderPasses::rpDepth)
            StartDepthPass();
        else if (pass == RenderPasses::rpColor)
            StartColorPass();
        else
            StartFullPass();
    }

    RenderPasses RenderPass(void) noexcept { return m_renderPass; }

    virtual bool Start3DScene(void);

    virtual bool Stop3DScene(void);

    virtual bool Start2DScene(void);

    virtual bool Stop2DScene(void);

    virtual bool UsePostEffectShader(void) { return false; }

    virtual Shader* LoadPostEffectShader(void) { return nullptr; }

    inline void SetSceneViewport(Viewport viewport) noexcept {
        m_sceneViewport = viewport;
    }

    inline Viewport GetSceneViewport(void) noexcept {
        return m_sceneViewport;
    }

    virtual void Draw3DScene(void);

    virtual void RenderToViewport(Texture* texture, RGBAColor color, bool bRotate, bool bFlipVertically);

    virtual void DrawScreen(bool bRotate, bool bFlipVertically);

    virtual bool EnableCamera(void) { return false; }

    virtual bool DisableCamera(void) { return false; }

    inline FBO* SceneBuffer(void) noexcept { return m_sceneBuffer; }

    inline FBO* ScreenBuffer(void) noexcept { return m_screenBuffer; }

    bool SetActiveBuffer(FBO* buffer, bool clearBuffer = false);

    inline int WindowWidth(void) noexcept { return m_windowWidth; }

    inline int WindowHeight(void) noexcept { return m_windowHeight; }

    inline int SceneWidth(void) noexcept { return m_sceneWidth; }

    inline int SceneHeight(void) noexcept { return m_sceneHeight; }

    inline int SceneLeft(void) noexcept { return m_sceneLeft; }

    inline int SceneTop(void) noexcept { return m_sceneTop; }

    inline float AspectRatio(void) noexcept { return m_aspectRatio; }

    inline Matrix4f& ViewportTransformation(void) noexcept { return m_viewport.Transformation(); }

    inline Vector2f& NDCScale(void) noexcept { return m_ndcScale; }

    inline Vector2f& NDCBias(void) noexcept { return m_ndcBias; }

    template <typename T>
    inline void SetBackgroundColor(T&& backgroundColor) {
        m_backgroundColor = std::forward<T>(backgroundColor);
    }

    inline void SetClearColor(const RGBAColor& color) const noexcept {
        glClearColor(color.R(), color.G(), color.B(), color.A());
    }

    inline void SetClearColor(RGBAColor&& color)  noexcept {
        SetClearColor(static_cast<const RGBAColor&>(color));
    }

    inline void ResetClearColor(void) noexcept {
        glClearColor(0, 0, 0, 0);
    }
#if 0
    typedef struct {
        int width, height;
    } tViewport;
#endif
    inline BaseQuad& RenderQuad(void)  noexcept { return m_renderQuad; }

    inline Viewport& Viewport(void)  noexcept { return m_viewport; }

    void SetViewport(bool flipVertically = false)
        noexcept;

    void SetViewport(::Viewport viewport, int windowWidth = 0, int windowHeight = 0, bool flipViewportVertically = false, bool flipWindowVertically = false) noexcept; // , bool isFBO = false);

    void PushViewport(void) {
        viewportStack.Append(m_viewport);
    }

    void PopViewport(void) {
        ::Viewport viewport;
        viewportStack.Pop(viewport);
        SetViewport(viewport, viewport.WindowWidth(), viewport.WindowHeight(), viewport.FlipVertically());
    }

    inline TexCoord ViewportSize(void) noexcept {
        return TexCoord(float(m_viewport.Width()), float(m_viewport.Height()));
    }

    inline TexCoord TexelSize(void) noexcept {
        return TexCoord(1.0f / float(m_viewport.Width()), 1.0f / (m_viewport.Height()));
    }

    void Render(Shader* shader, Texture* texture = nullptr, const RGBAColor& color = ColorData::White);

    void Render(Shader* shader, Texture* texture, RGBAColor&& color) {
        Render(shader, texture, static_cast<const RGBAColor&>(color));
    }

    void Fill(const RGBAColor& color, float scale = 1.0f);

    void Fill(RGBAColor&& color, float scale = 1.0f) {
        Fill(static_cast<const RGBAColor&>(color), scale);
    }

    template <typename T>
    inline void Fill(T&& color, float alpha, float scale = 1.0f) {
        Fill(RGBAColor(std::forward<T>(color), alpha), scale);
    }

    inline GLVersion GetGLVersion(void) noexcept {
        return m_glVersion;
    }

    inline void ShowFps(bool showFps) {
        m_frameCounter.ShowFps(showFps);
    }

    inline void ToggleFps(void) {
        m_frameCounter.Toggle();
    }

    static void ClearGLError(void) noexcept;

    static bool CheckGLError(const char* operation = "") noexcept;
};

using RenderPassType = BaseRenderer::RenderPasses;

#define baseRenderer BaseRenderer::Instance()

// =================================================================================================
