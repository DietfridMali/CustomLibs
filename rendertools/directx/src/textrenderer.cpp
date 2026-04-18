//pragma once

#include <algorithm>
#include "conversions.hpp"
#include "base_renderer.h"
#include "base_shaderhandler.h"
#include "colordata.h"
#include "textrenderer.h"
#include "base_renderer.h"
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
    m_mesh.Init(MeshTopology::Quads, 100);
    m_mesh.SetDynamic(true);
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
    Shader* shader = LoadShader();
    if (not shader)
        return;

    // test code: display entire atlas on screen
#if TEST_ATLAS
    baseRenderer.ResetTransformation();
    baseRenderer.SetViewport(::Viewport (0, 0, m_font->GetAtlas().GetWidth(), m_font->GetAtlas().GetHeight()));
    gfxDriverStates.SetBlending(0);
    gfxDriverStates.SetFaceCulling(0);
    gfxDriverStates.SetDepthTest(0);
    gfxDriverStates.SetDepthWrite(0);
    RenderTarget* renderTarget = m_font->GetRenderTarget();
    if (renderTarget) {
        renderTarget->Render({}, ColorData::Yellow);
        //delete renderTarget;
    }
    return;
#endif

    if (flipVertically)
        y = -y;

    m_mesh.ResetGfxData();
    for (auto glyph : text) {
        FontHandler::GlyphInfo* info = m_font->FindGlyph(String(glyph));

        if (info) {
            // create output quad coordinates
            float w = float(info->glyphSize.width) * scale;
            Vector3f p{ x, y, 0.0f };
            m_mesh.AddVertex(p);
            p.X() += w;
            m_mesh.AddVertex(p);
            p.Y() = -y;
            m_mesh.AddVertex(p);
            p.X() = x;
            m_mesh.AddVertex(p);
            x += w;

            // create input tex coords of the glyph in the atlas
            TexCoord tc{ info->atlasPosition };
            m_mesh.AddTexCoord(tc);
            tc.X() += info->atlasSize.X();
            m_mesh.AddTexCoord(tc);
            tc.Y() += info->atlasSize.Y();
            m_mesh.AddTexCoord(tc);
            tc.X() = info->atlasPosition.X();
            m_mesh.AddTexCoord(tc);
        }
    }
    m_mesh.UpdateData(true);
    gfxDriverStates.SetDepthTest(0);
    gfxDriverStates.SetDepthWrite(0);
    m_mesh.Render(m_font->GetTexture());
}


BaseQuad& TextRenderer::CreateQuad(BaseQuad& q, float x, float y, float w, Texture* t, bool flipVertically) {
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
    BaseQuad q;
    for (auto glyph : text) {
        FontHandler::GlyphInfo* info = m_font->FindGlyph(String(glyph));
        if (info->index < 0)
            fprintf(stderr, "TextRenderer: Texture for glyph '%c' not found.\r\n", glyph);
        else {
            float width = float(info->glyphSize.width) * scale;
            CreateQuad(q, x, y, width, info->texture, flipVertically);
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


void TextRenderer::RenderText(String& text, int textWidth, float xOffset, float yOffset, eTextAlignments alignment, int flipVertically) {
    baseRenderer.PushMatrix();
#if !TEST_ATLAS
    baseRenderer.ResetTransformation();
    baseRenderer.Translate(0.5f, 0.5f, 0.0f);
#endif
    gfxDriverStates.DepthFunc(GL_ALWAYS);
    float letterScale = 2 * xOffset / float(textWidth);
    // reusing xOffset here
    if (alignment == taLeft)
        xOffset = -0.5;
    else if (alignment == taCenter)
        xOffset = -xOffset;
    else
        xOffset = 0.5f - 2 * xOffset;
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

        if (not renderTarget)
            RenderText(text, td.width, offset.x, offset.y, alignment, flipVertically);
        else {
#if 0
            renderTarget->SetClearColor(RGBAColor(1.0f, 0.8f, 0.0f, 1.0f));
#endif
            if (renderTarget->Enable(-1, RenderTarget::dbAll, true)) {
                baseRenderer.PushViewport();
                renderTarget->SetViewport();
                if (outlineWidth > 0) {
                    offset.x -= outlineWidth / float(renderTarget->m_width);
                    offset.y -= outlineWidth / float(renderTarget->m_height);
                }
                renderTarget->m_lastDestination = 0;
                RenderText(text, td.width, offset.x, offset.y, alignment);
                uint8_t postProcess = HaveOutline() ? 1 : ApplyAA() ? 2 : 0;
#ifndef OPENGL
                renderTarget->Disable(true);
#else
                renderTarget->Disable();
#endif
                postProcess = 0;
                if (postProcess != 0) {
#ifndef OPENGL
                    renderTarget->Enable(-1, RenderTarget::dbAll, false);
                    //renderTarget->SetViewport();
#endif
                    if (postProcess == 1)
                        RenderOutline(renderTarget, m_decoration);
                    else
                        AntiAlias(renderTarget, m_decoration.aaMethod);
#ifndef OPENGL
                    renderTarget->Disable(true);
#endif
                }
                baseRenderer.PopViewport();
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
