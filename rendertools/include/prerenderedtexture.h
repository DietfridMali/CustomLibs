#pragma once


#include "glew.h"
#include "vector.hpp"
#include "colordata.h"
#include "rectangle.h"
#include "base_quad.h"
#include "texture.h"
#include "viewport.h"
#include "fbo.h"
#include "textrenderer.h"

// =================================================================================================

class PrerenderedItem {
public:
    FBO         m_fbo;
    Viewport    m_viewport;
    int         m_bufferCount;

    PrerenderedItem()
        : m_bufferCount(0)
    { }

    PrerenderedItem(Viewport& viewport)
        : m_bufferCount(0), m_viewport(viewport)
    { }


    FBO& GetFBO(void) noexcept {
        return m_fbo;
    }

    void SetViewport(Viewport& viewport) noexcept {
        m_viewport = viewport;
    }

    void SetBufferCount(int bufferCount) noexcept {
        m_bufferCount = bufferCount;
    }

    bool Create(int bufferCount = 1);

    void Destroy() {
        m_fbo.Destroy();
    }

    virtual void Render(void) {}
};

// =================================================================================================

class PrerenderedText 
    : public PrerenderedItem 
{
public:
    String          m_text;
    int             m_bufferCount;
    RGBAColor       m_color;
    float           m_scale;

    PrerenderedText(int bufferCount = 0, Viewport viewport = Viewport(), float scale = 1.0f);

    void Destroy() {
        m_text.Destroy();
        PrerenderedItem::Destroy();
    }

    bool Create(String text, TextRenderer::eTextAlignments alignment = TextRenderer::taCenter, RGBAColor color = ColorData::White, const TextRenderer::TextDecoration& decoration = {});

    inline void SetColor(RGBAColor color) noexcept {
        m_color = color;
    }

    inline void SetAlpha(float alpha) noexcept {
        m_color.A() = alpha;
    }

    void SetScale(float scale = 1.0) noexcept {
        m_scale = scale;
    }

    void RenderOutline(const TextRenderer::TextDecoration& decoration = {});

    virtual void Render(bool setViewport = true, int flipVertically = 0, RGBAColor color = ColorData::Invisible, float scale = 0.0f);
};

// =================================================================================================

class PrerenderedImage : public PrerenderedItem {
public:
    Texture     m_image;
    RGBAColor   m_backgroundColor;
    float       m_scale;
    BaseQuad   m_viewportArea;

    PrerenderedImage(Texture& image, Viewport viewport, RGBAColor backgroundColor = ColorData::White, float scale = 1.0f)
        : PrerenderedItem(viewport), m_image(image), m_backgroundColor(backgroundColor), m_scale(scale)
    {
    }

    void Create(void);

    virtual void Render(void);
};

// =================================================================================================


