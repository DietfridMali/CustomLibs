
#include "glew.h"
#include "vector.hpp"
#include "base_quad.h"
#include "texture.h"
#include "viewport.h"
#include "fbo.h"
#include "base_renderer.h"
#include "textrenderer.h"
#include "prerenderedtexture.h"

// =================================================================================================

bool PrerenderedItem::Create(int bufferCount) {
    if (m_fbo.IsAvailable() and (m_bufferCount == bufferCount)) {
        if (m_fbo.GetViewport() == m_viewport) {
            m_fbo.SetLastDestination(0);
            return false;
        }
        m_fbo.Destroy();
    }
    m_bufferCount = bufferCount;
    m_fbo.Create(m_viewport.m_width, m_viewport.m_height, 2, { .colorBufferCount = bufferCount } );
    return true;
}

// =================================================================================================

PrerenderedText::PrerenderedText(int bufferCount, Viewport viewport, float scale)
    : PrerenderedItem(viewport)
    , m_bufferCount(bufferCount), m_scale(scale), m_text("")
{ }

bool PrerenderedText::Create(String text, TextRenderer::eTextAlignments alignment, RGBAColor color, const TextRenderer::TextDecoration& decoration) {
    if (m_bufferCount == 0)
        m_bufferCount = 2;//  (m_outlineWidth == 0) ? 1 : 2;
    if (not PrerenderedItem::Create(m_bufferCount) and (m_text == text))
        return false;
    m_text = text;
    m_color = color;
    textRenderer.SetColor(m_color);
    textRenderer.SetDecoration(decoration);
    textRenderer.SetScale(1.0f);
    textRenderer.RenderToBuffer(m_text, alignment, &m_fbo, m_fbo.m_viewport, 0, 0); // m_outlineWidth == 0);
    /*textRenderer.SetColor();*/
    return true;
}


void PrerenderedText::RenderOutline(const TextRenderer::TextDecoration& decoration) {
    if (decoration.HaveOutline()) {
        baseRenderer.PushViewport();
        m_fbo.SetViewport();
        m_fbo.SetLastDestination(0);
        textRenderer.SetDecoration(decoration);
        textRenderer.RenderOutline(&m_fbo, decoration);
        baseRenderer.PopViewport();
    }
}


void PrerenderedText::Render(bool setViewport, int flipVertically, RGBAColor color, float scale) {
    textRenderer.SetColor(color.IsVisible() ? color : m_color);
    textRenderer.SetScale((scale > 0.0f) ? scale : m_scale);
    if (setViewport)
        m_viewport.SetViewport();
    textRenderer.RenderToScreen(&m_fbo, flipVertically); // m_outlineWidth == 0);
}

// =================================================================================================

void PrerenderedImage::Create(void) {
    PrerenderedItem::Create();
    m_viewport.SetViewport();
    m_viewport.Fill(static_cast<RGBColor>(m_backgroundColor), 1);
    m_fbo.RenderTexture(&m_image, { .destination = 0, .clearBuffer = true });
}


void PrerenderedImage::Render(void) {
    m_viewport.SetViewport();
    m_fbo.Render({ .source = 0, .destination = -1 });
}

// =================================================================================================
