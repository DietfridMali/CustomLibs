#pragma once


#include "vector.hpp"
#include "colordata.h"
#include "rectangle.h"
#include "base_quad.h"
#include "texture.h"
#include "viewport.h"
#include "rendertarget.h"
#include "textrenderer.h"

// =================================================================================================

class PrerenderedItem {
public:
    RenderTarget    m_renderTarget;
    Viewport        m_viewport;
    int             m_bufferCount;

    PrerenderedItem()
        : m_bufferCount(0)
    {}

    PrerenderedItem(Viewport& viewport)
        : m_bufferCount(0), m_viewport(viewport)
    {}


    RenderTarget& GetRenderTarget(void) noexcept {
        return m_renderTarget;
    }

    void SetViewport(Viewport& viewport) noexcept {
        m_viewport = viewport;
    }

    void SetBufferCount(int bufferCount) noexcept {
        m_bufferCount = bufferCount;
    }

    bool Create(int bufferCount = 1);

    bool Create(int width, int height, int scale, int bufferCount);

    void Destroy() {
        m_renderTarget.Destroy();
    }

    virtual void Render(void) {}

    void RenderOutline(const TextEffects::Decoration& decoration);

    void RenderBevel(int bevelWidth, const Vector2f& lightDir, float strength);
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

    ~PrerenderedText() {
        Destroy();
	}

    void Destroy() {
        m_text.Destroy();
        PrerenderedItem::Destroy();
    }

    bool Create(String text, TextRenderer::eTextAlignments alignment = TextRenderer::taCenter, RGBAColor color = ColorData::White, const TextEffects::Decoration& decoration = {});

    inline void SetColor(RGBAColor color) noexcept {
        m_color = color;
    }

    inline void SetAlpha(float alpha) noexcept {
        m_color.A() = alpha;
    }

    void SetScale(float scale = 1.0) noexcept {
        m_scale = scale;
    }

    virtual void Render(bool setViewport = true, int flipVertically = 0, RGBAColor color = ColorData::Invisible, float scale = 0.0f);
};

// =================================================================================================

class PrerenderedImage : public PrerenderedItem {
public:
    Texture*    m_image;
    RGBAColor   m_backgroundColor;
    RGBAColor   m_outlineColor;

    PrerenderedImage()
        : m_backgroundColor(ColorData::White)
    {
    }

    PrerenderedImage(Texture* image, RGBAColor backgroundColor = ColorData::Invisible)
        : PrerenderedItem()
        , m_image(image)
        , m_backgroundColor(backgroundColor)
    {
    }

    void Create(int outlineWidth);

    inline bool IsAvailable(void) noexcept {
        return m_renderTarget.IsAvailable();
    }

    void Render(RGBAColor color);

    virtual void Render(void);
};

// =================================================================================================


