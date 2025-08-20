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
#include "framecounter.h"

// =================================================================================================
// basic renderer class. Initializes display and OpenGL and sets up projections and view matrix

class BaseRenderer 
    : public RenderMatrices
    , public DrawBufferHandler
    , public PolymorphSingleton<BaseRenderer>
{
    public:
        struct GLVersion {
            GLint major{ 0 };
            GLint minor{ 0 };
        };

    protected:
        FBO*                    m_screenBuffer;
        FBO*                    m_sceneBuffer;
        Texture                 m_renderTexture;
        bool                    m_screenIsAvailable;

        Viewport                m_viewport;
        BaseQuad                m_viewportArea;

        int                     m_windowWidth;
        int                     m_windowHeight;
        int                     m_sceneWidth;
        int                     m_sceneHeight;
        int                     m_sceneLeft;
        int                     m_sceneTop;
        float                   m_aspectRatio;

        GLVersion               m_glVersion;
        MovingFrameCounter      m_frameCounter;

        static List<::Viewport> viewportStack;
public:
        BaseRenderer()
            : m_screenBuffer(nullptr), m_sceneBuffer(nullptr)
            , m_windowWidth(0), m_windowHeight(0), m_sceneWidth(0), m_sceneHeight(0), m_sceneLeft(0), m_sceneTop(0), m_aspectRatio(1.0f)
            , m_screenIsAvailable(false)
        { 
            //_instance = this;
        }

        static BaseRenderer& Instance(void) { return dynamic_cast<BaseRenderer&>(PolymorphSingleton::Instance()); }

        bool InitOpenGL(void);

        virtual void Init(int width, int height, float fov);

        virtual bool Create(int width = 1920, int height = 1080, float fov = 45);

        bool CreateScreenBuffer(void);
            
        void SetupOpenGL (void);

        virtual bool Start3DScene(void);

        virtual bool Stop3DScene(void);

        virtual bool Start2DScene(void);

        virtual bool Stop2DScene(void);

        virtual void Draw3DScene(void);
            
        virtual void DrawScreen(bool bRotate, bool bFlipVertically);

        virtual bool EnableCamera(void) { return false; }

        virtual bool DisableCamera(void) { return false; }

        inline FBO* SceneBuffer(void) { return m_sceneBuffer; }

        inline FBO* ScreenBuffer(void) { return m_screenBuffer; }

        bool SetActiveBuffer(FBO* buffer, bool clearBuffer = false);

        inline int WindowWidth(void) { return m_windowWidth; }

        inline int WindowHeight(void) { return m_windowHeight; }

        inline int SceneWidth(void) { return m_sceneWidth; }

        inline int SceneHeight(void) { return m_sceneHeight; }

        inline float AspectRatio(void) { return m_aspectRatio; }
#if 0
        typedef struct {
            int width, height;
        } tViewport;
#endif
        Viewport& Viewport(void) { return m_viewport; }

        void SetViewport(bool flipVertically = false);

        void SetViewport(::Viewport viewport, bool flipVertically = false); // , bool isFBO = false);

        void Fill(const RGBAColor& color, float scale = 1.0f);

        void Fill(RGBAColor&& color, float scale = 1.0f) {
            Fill(static_cast<const RGBAColor&>(color), scale);
        }

        template <typename T>
        inline void Fill(T&& color, float alpha, float scale = 1.0f) {
            Fill(RGBAColor(std::forward<T>(color), alpha), scale);
        }

        void PushViewport(void) {
            viewportStack.Append(m_viewport);
        }

        void PopViewport(void) {
            ::Viewport viewport;
            viewportStack.Pop(viewport);
            SetViewport(viewport);
        }

        inline GLVersion GetGLVersion(void) {
            return m_glVersion;
        }

        inline void ShowFps(bool showFps) {
            m_frameCounter.ShowFps(showFps);
        }

        static void ClearGLError(void);

        static bool CheckGLError (const char* operation = "");
};

#define baseRenderer BaseRenderer::Instance()

// =================================================================================================
