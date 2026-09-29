//pragma once

#include <algorithm>
#include "conversions.hpp"
#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "colordata.h"
#include "textrenderer.h"
#include "meshhandler.h"
#include "gfxrenderer.h"
#include "tristate.h"

#ifndef _WIN32
#   include <locale>
#endif

#define USE_TEXT_RTS 1
#define USE_ATLAS 1
#define TEST_ATLAS 0

using GlyphSize = TextureAtlas::GlyphSize;
using TextDimensions = TextureAtlas::GlyphSize;

// =================================================================================================

int TextRenderer::CompareRenderTargets(void* context, const int& key1, const int& key2) {
    return (key1 < key2) ? -1 : (key1 > key2) ? 1 : 0;
}


TextRenderer::TextRenderer(RGBAColor color, const TextDecoration& decoration, float scale)
    : m_color(color), m_scale(scale), m_font(nullptr), m_textAlignment(taCenter), m_decoration(decoration)
{
}


RenderTarget* TextRenderer::GetRenderTarget(int scale) {
    if (m_renderTarget)
        delete m_renderTarget;
    m_renderTarget = new RenderTarget();
    m_renderTarget->Create(baseRenderer.GetViewport().m_width, baseRenderer.GetViewport().m_height, scale, {.name = "text", .colorBufferCount = 2});
    return m_renderTarget;
}


Shader* TextRenderer::LoadShader(void) {
    return baseShaderHandler.LoadPlainTextureShader(m_color);
}


void TextRenderer::RenderTextMesh(String& text, float x, float y, float scale, bool flipVertically) {
    if (not m_font)
        return;
    baseRenderer.Set2DRenderStates();
    bool haveGlyphColors = HaveGlyphColors();
    Shader* shader = haveGlyphColors ? baseShaderHandler.LoadColoredTextureShader(m_color) : LoadShader();
    if (not shader)
        return;

    if (flipVertically)
        y = -y;

    uint32_t meshBuffers = Mesh::mbIndex | Mesh::mbVertex | Mesh::mbTexCoord0;
    if (haveGlyphColors)
        meshBuffers |= Mesh::mbColor;
    Mesh* mesh = meshHandler.AllocMesh(meshBuffers);
    int32_t glyphIndex = 0;
    for (int32_t offset = 0; offset < int32_t(text.Length()); ) {
        FontHandler::GlyphInfo* info = m_font->FindGlyph(FontHandler::NextGlyph(text, offset));

        if (info) {
            // create output quad coordinates
            float w = float(info->glyphSize.width) * scale;
            Vector3f p{ x, y, 0.0f };
            mesh->AddVertex(p);
            p.X() += w;
            mesh->AddVertex(p);
            p.Y() = -y;
            mesh->AddVertex(p);
            p.X() = x;
            mesh->AddVertex(p);
            x += w;

            // create input tex coords of the glyph in the atlas; sample only the ink band so the font's
            // metric padding (loose ascent/descent) is not scaled into the viewport. InkTop()/InkHeight()
            // are fractions of the glyph cell height (0 / 1 = full cell = no correction).
            float atlasTop = info->atlasPosition.Y() + m_font->InkTop() * info->atlasSize.Y();
            float atlasBottom = atlasTop + m_font->InkHeight() * info->atlasSize.Y();
            TexCoord tc{ info->atlasPosition.X(), atlasTop };
            mesh->AddTexCoord(tc);
            tc.X() += info->atlasSize.X();
            mesh->AddTexCoord(tc);
            tc.Y() = atlasBottom;
            mesh->AddTexCoord(tc);
            tc.X() = info->atlasPosition.X();
            mesh->AddTexCoord(tc);

            if (haveGlyphColors) {
                RGBAColor& color = GlyphColor(glyphIndex);
                for (int32_t i = 0; i < 4; ++i)
                    mesh->AddColor(color);
            }
            ++glyphIndex;
        }
    }
    mesh->UpdateData(true);
    mesh->Render(m_font->GetAsTexture());
}


BaseQuadMesh& TextRenderer::CreateQuad(BaseQuadMesh& q, float x, float y, float w, Texture* t, bool flipVertically) {
    if (flipVertically)
        q.Setup({ Vector3f{x, y, 0.0}, Vector3f{x + w, y, 0.0}, Vector3f{x + w, -y, 0.0}, Vector3f{x, -y, 0.0} },
                { TexCoord{0, 1}, TexCoord{1, 1}, TexCoord{1, 0}, TexCoord{0, 0} });
    else
        q.Setup({ Vector3f{x, y, 0.0}, Vector3f{x + w, y, 0.0}, Vector3f{x + w, -y, 0.0}, Vector3f{x, -y, 0.0} },
                { TexCoord{ 0, 0 }, TexCoord{ 1, 0 }, TexCoord{ 1, 1 }, TexCoord{ 0, 1 } });
    q.GetGfxDataLayout().SetDynamic(true);
    return q;
}


void TextRenderer::RenderGlyphs(String& text, float x, float y, float scale, bool flipVertically) {
    Shader* shader = LoadShader();
    if (not shader)
        return;
    BaseQuadMesh q;
    int32_t glyphIndex = 0;
    for (int32_t offset = 0; offset < int32_t(text.Length()); ) {
        String glyph = FontHandler::NextGlyph(text, offset);
        FontHandler::GlyphInfo* info = m_font->FindGlyph(glyph);
        if ((info == nullptr) or (info->index < 0))
            fprintf(stderr, "TextRenderer: Texture for glyph '%s' not found.\r\n", (const char*)glyph);
        else {
            float width = float(info->glyphSize.width) * scale;
            CreateQuad(q, x, y, width, info->texture, flipVertically);
            if (HaveGlyphColors())
                shader = baseShaderHandler.LoadPlainTextureShader(GlyphColor(glyphIndex));
            ++glyphIndex;
#if 1
            q.Render(shader, info->texture, m_color);
#else
            q.Fill((i & 1) ? ColorData::Orange : ColorData::MediumBlue);
#endif
            x += width;
        }
    }
}


float TextRenderer::XOffset(float xOffset, int textWidth, eTextAlignments alignment) {
#if 1
    if (alignment == taLeft)
        return -0.5;
    if (alignment == taCenter)
        return -xOffset;
#endif
    return 0.5f - 2 * xOffset;
}


void TextRenderer::RenderText(String& text, int textWidth, float xOffset, float yOffset, eTextAlignments alignment, int flipVertically, float xMargin) {
    baseRenderer.PushMatrix();
#if !TEST_ATLAS
    baseRenderer.ResetTransformation();
    baseRenderer.Translate(0.5f, 0.5f, 0.0f);
#endif
    gfxStates.DepthFunc(GfxOperations::CompareFunc::Always);
    float letterScale = 2 * xOffset / float(textWidth);
    // reusing xOffset here
    if (alignment == taLeft)
        xOffset = -0.5f + xMargin;
    else if (alignment == taCenter)
        xOffset = -xOffset;
    else
        xOffset = 0.5f - 2 * xOffset - xMargin;
#if USE_ATLAS
    RenderTextMesh(text, xOffset, yOffset, letterScale, flipVertically < 0);
#else
    RenderGlyphs(text, xOffset, yOffset, letterScale, flipVertically < 0);
#endif
    baseRenderer.PopMatrix();
}


int TextRenderer::SourceBuffer(bool hasOutline, bool antiAliased) {
    return hasOutline ? antiAliased ? 0 : 1 : antiAliased ? 1 : 0;
}


void TextRenderer::Fill(Vector4f color) {
    RenderTarget* renderTarget = GetRenderTarget(1);
    if (renderTarget != nullptr)
        renderTarget->Fill(color);
}


float TextRenderer::FitScale(String text, int viewportWidth, int viewportHeight, const TextDecoration& decoration) {
    if (not m_font)
        return 0.0f;
    TextDimensions td = m_font->TextSize(text);
    float margin = float(int(4 * decoration.outlineWidth + 0.5f));
    return std::min((float(viewportWidth) - margin) / float(td.width), (float(viewportHeight) - margin) / float(td.height));
}


void TextRenderer::RenderToBuffer(String text, eTextAlignments alignment, RenderTarget* renderTarget, Viewport& viewport, int renderAreaWidth, int renderAreaHeight, int flipVertically) {
    if (m_font) {
        if (renderTarget)
            renderTarget->m_name = String::Concat ("[", text, "]");
        TextDimensions td = m_font->TextSize(text);
        float outlineWidth = m_decoration.outlineWidth * 2;
        td.width += int(2 * outlineWidth + 0.5f);
        td.height += int(2 * outlineWidth + 0.5f);
        RenderOffsets offset =
#if 1
            Texture::ComputeOffsets(int(td.height * td.aspectRatio), td.height, viewport.m_width, viewport.m_height, renderAreaWidth, renderAreaHeight);
#else
            m_centerText
            ? Texture::ComputeOffsets(int(td.height * td.aspectRatio), td.height, viewport.m_width, viewport.m_height, renderAreaWidth, renderAreaHeight)
            : Texture::ComputeOffsets(int(td.height * td.aspectRatio), td.height, int(textHeight * aspectRatio), textHeight, int(textHeight * aspectRatio), textHeight);
#endif
        td.width -= int(2 * outlineWidth + 0.5f);
        td.height -= int(2 * outlineWidth + 0.5f);

        if (not renderTarget) {
#if 1
            void* cl;
            if (baseRenderer.StartOperation(&cl, text)) {
                RenderText(text, td.width, offset.x, offset.y, alignment, flipVertically);
                baseRenderer.FinishOperation(cl);
            }
#endif
        }
        else {
#if 0 // debug
            renderTarget->SetClearColor(RGBAColor(1.0f, 0.8f, 0.0f, 1.0f));
#endif
            if (renderTarget->Activate({ .clear = true })) {
                float xMargin = 0.0f;
                if (outlineWidth > 0) {
                    int margin = int(2 * outlineWidth + 0.5f);
                    int areaWidth = (renderAreaWidth > 0) ? renderAreaWidth : viewport.m_width;
                    int areaHeight = (renderAreaHeight > 0) ? renderAreaHeight : viewport.m_height;
                    offset = Texture::ComputeOffsets(td.width, td.height, viewport.m_width, viewport.m_height, areaWidth - margin, areaHeight - margin);
                    xMargin = outlineWidth / float(viewport.m_width);
                }
                renderTarget->m_lastDestination = 0;
                RenderText(text, td.width, offset.x, offset.y, alignment, flipVertically, xMargin);
                uint8_t postProcess = HaveOutline() ? 1 : ApplyAA() ? 2 : 0;
#if 0 // debug
                postProcess = 0;
#endif
                if (postProcess != 0) {
                    if (postProcess == 1)
                        RenderOutline(renderTarget, m_decoration);
                    else
                        AntiAlias(renderTarget, m_decoration.aaMethod);
                }
                renderTarget->Deactivate();
            }
        }
    }
}


void TextRenderer::RenderToScreen(RenderTarget* renderTarget, int flipVertically) {
#if USE_TEXT_RTS
    if (m_font)
        renderTarget->Render({ .source = renderTarget ? renderTarget->GetLastDestination() : -1, .clearBuffer = false, .flipVertically = flipVertically, .scale = m_scale }, m_color); // render outline to viewport
#endif
}


void TextRenderer::Render(String text, eTextAlignments alignment, int flipVertically, int renderAreaWidth, int renderAreaHeight, bool useRenderTarget) {
    if (m_font and (text.Length() > 0)) {
        if (not useRenderTarget)
            RenderToBuffer(text, alignment, nullptr, baseRenderer.GetViewport(), renderAreaWidth, renderAreaHeight, flipVertically);
        else {
            GetRenderTarget(2);
            if (m_renderTarget != nullptr) {
                RenderToBuffer(text, alignment, m_renderTarget, baseRenderer.GetViewport(), renderAreaWidth, renderAreaHeight);
                RenderToScreen(m_renderTarget, flipVertically); // render outline to viewport
            }
        }
    }
}

// =================================================================================================
