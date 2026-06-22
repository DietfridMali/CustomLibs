
#include "vector.hpp"
#include "base_quad.h"
#include "texture.h"
#include "viewport.h"
#include "rendertarget.h"
#include "gfxrenderer.h"
#include "textrenderer.h"
#include "base_shaderhandler.h"
#include "prerenderedtexture.h"

// =================================================================================================

bool PrerenderedItem::Create(int width, int height, int scale, int bufferCount) {
    return m_renderTarget.Create(width, height, 2, { .colorBufferCount = bufferCount });
}


bool PrerenderedItem::Create(int bufferCount) {
    if (m_renderTarget.IsAvailable() and (m_bufferCount == bufferCount)) {
        if (m_renderTarget.GetViewport() == m_viewport) {
            m_renderTarget.SetLastDestination(0);
            return false;
        }
        m_renderTarget.Destroy();
    }
    m_bufferCount = bufferCount;
    return Create(m_viewport.m_width, m_viewport.m_height, 2, bufferCount );
    return true;
}


void PrerenderedItem::RenderOutline(const TextEffects::Decoration & decoration) {
    if (decoration.HaveOutline()) {
        m_renderTarget.Activate({ .clear = false });
        TextEffects().RenderOutline(&m_renderTarget, decoration);
        m_renderTarget.Deactivate();
    }
}


// Raised / "reverse emboss" look via the "bevel" shader. Chains like RenderOutline: reads the current
// last-destination buffer and writes the next (AutoRender), so call it between Create and RenderOutline
// (Create -> buffer 0, RenderBevel -> buffer 1, RenderOutline -> buffer 0).
void PrerenderedItem::RenderBevel(int bevelWidth, const Vector2f& lightDir, float strength) {
    if (bevelWidth <= 0)
        return;
    m_renderTarget.Activate({ .clear = false });
    baseRenderer.Set2DRenderStates();
    Shader* shader = baseShaderHandler.SetupRenderShader("bevel");
    if (shader and not baseRenderer.IsShadowPass()) {
        if (baseRenderer.HasOpenGL())
            shader->SetInt("surface", 0);
        shader->SetVector2f("texelSize", m_renderTarget.TexelSize());
        shader->SetVector2f("lightDir", lightDir);
        shader->SetFloat("bevelWidth", float(bevelWidth));
        shader->SetFloat("strength", strength);
        m_renderTarget.AutoRender({ .clearBuffer = true, .shader = shader });
    }
    m_renderTarget.Deactivate();
}

// =================================================================================================

PrerenderedText::PrerenderedText(int bufferCount, Viewport viewport, float scale)
    : PrerenderedItem(viewport)
    , m_bufferCount(bufferCount)
    , m_scale(scale)
    , m_text("")
{ }


bool PrerenderedText::Create(String text, TextRenderer::eTextAlignments alignment, RGBAColor color, const TextEffects::Decoration& decoration) {
    if (m_bufferCount == 0)
        m_bufferCount = 2;//  (m_outlineWidth == 0) ? 1 : 2;
    if (not PrerenderedItem::Create(m_bufferCount) and (m_text == text))
        return false;
    m_text = text;
    m_color = color;
    textRenderer.SetColor(m_color);
    textRenderer.SetDecoration(decoration);
    textRenderer.SetScale(1.0f);
    textRenderer.RenderToBuffer(m_text, alignment, &m_renderTarget, m_renderTarget.m_viewport, 0, 0, /*baseRenderer.HasOpenGL() ? -1 : */ 1); // m_outlineWidth == 0);
    /*textRenderer.SetColor();*/
    return true;
}


void PrerenderedText::Render(bool setViewport, int flipVertically, RGBAColor color, float scale) {
    textRenderer.SetColor(color.IsVisible() ? color : m_color);
    textRenderer.SetScale((scale > 0.0f) ? scale : m_scale);
    if (setViewport)
        m_viewport.SetViewport();
    textRenderer.RenderToScreen(&m_renderTarget, flipVertically); // m_outlineWidth == 0);
}

// =================================================================================================

void PrerenderedImage::Create(int outlineWidth) {
    m_renderTarget.SetClearColor(m_backgroundColor);
    PrerenderedItem::Create(m_image->GetWidth() + 2 * outlineWidth, m_image->GetHeight() + 2 * outlineWidth, 1, 2);
    m_renderTarget.Activate({});
    float scale = float(m_image->GetWidth()) / float(m_image->GetWidth() + 2 * outlineWidth);
    m_renderTarget.RenderAsTexture(m_image, { .destination = 0, .clearBuffer = true, .scale = scale });
    m_renderTarget.Deactivate();
}


void PrerenderedImage::Render(void) {
    Render(ColorData::White);
}


// draws the (outlined) result into the CURRENT viewport — the caller sets it (so it follows window resizes)
void PrerenderedImage::Render(RGBAColor color) {
    m_renderTarget.Render({ .source = m_renderTarget.GetLastDestination(), .destination = -1}, color);
}

// =================================================================================================
