//pragma once

#include <algorithm>
#include "glew.h"
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

#define USE_TEXT_FBOS 1
#define USE_ATLAS 1
#define TEST_ATLAS 0

using GlyphSize = TextureAtlas::GlyphSize;
using TextDimensions = TextureAtlas::GlyphSize;

// =================================================================================================

int TextRenderer::CompareFBOs(void* context, const int& key1, const int& key2) {
    return (key1 < key2) ? -1 : (key1 > key2) ? 1 : 0;
}


TextRenderer::TextRenderer(RGBAColor color, const TextDecoration& decoration, float scale)
    : m_color(color), m_scale(scale), m_font(nullptr), m_textAlignment(taCenter), m_decoration(decoration)
{ 
    m_mesh.Init(GL_QUADS, 100);
    m_mesh.SetDynamic(true);
}


FBO* TextRenderer::GetFBO(float scale) {
    FBO** fboRef = m_fbos.Find(FBOID(baseRenderer.GetViewport().m_width, baseRenderer.GetViewport().m_height));
    if (fboRef != nullptr)
        return *fboRef;
    FBO* fbo = new FBO();
    fbo->Create(baseRenderer.GetViewport().m_width, baseRenderer.GetViewport().m_height, 2, {.name = "text", .colorBufferCount = 2});
    m_fbos.Insert(FBOID(fbo), fbo);
    return fbo;
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
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    baseRenderer.ClearGLError();
    m_font->GetFBO()->Render({}, ColorData::Yellow);
    return;
#endif

    if (flipVertically)
        y = -y;

    m_mesh.ResetVAO();
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
    m_mesh.UpdateVAO(true);
    m_mesh.Render(m_font->GetTexture());
}


BaseQuad& TextRenderer::CreateQuad(BaseQuad& q, float x, float y, float w, Texture* t, bool flipVertically) {
    if (flipVertically)
        q.Setup({ Vector3f{x, y, 0.0}, Vector3f{x + w, y, 0.0}, Vector3f{x + w, -y, 0.0}, Vector3f{x, -y, 0.0} },
                { TexCoord{0, 1}, TexCoord{1, 1}, TexCoord{1, 0}, TexCoord{0, 0} });
    else
        q.Setup({ Vector3f{x, y, 0.0}, Vector3f{x + w, y, 0.0}, Vector3f{x + w, -y, 0.0}, Vector3f{x, -y, 0.0} },
                { TexCoord{ 0, 0 }, TexCoord{ 1, 0 }, TexCoord{ 1, 1 }, TexCoord{ 0, 1 } });
    q.GetVAO().SetDynamic(true);
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
            fprintf(stderr, "texture '%c' not found\r\n", glyph);
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
    Tristate<GLenum> depthFunc (GL_NONE, GL_LEQUAL, openGLStates.DepthFunc(GL_ALWAYS));
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
    openGLStates.DepthFunc(depthFunc);
}


int TextRenderer::SourceBuffer(bool hasOutline, bool antiAliased) {
    return hasOutline ? antiAliased ? 0 : 1 : antiAliased ? 1 : 0;
}


void TextRenderer::Fill(Vector4f color) {
    FBO* fbo = GetFBO(1);
    if (fbo != nullptr)
        fbo->Fill(color);
}


void TextRenderer::RenderToBuffer(String text, eTextAlignments alignment, FBO* fbo, Viewport& viewport, int renderAreaWidth, int renderAreaHeight, int flipVertically) {
    if (m_font) {
        if (fbo)
            fbo->m_name = String::Concat ("[", text, "]");
        TextDimensions td = m_font->TextSize(text);
        float outlineWidth = m_decoration.outlineWidth * 2;
        td.width += int(2 * outlineWidth + 0.5f);
        td.height += int(2 * outlineWidth + 0.5f);
        tRenderOffsets offset =
#if 1
            Texture::ComputeOffsets(int(td.height * td.aspectRatio), td.height, viewport.m_width, viewport.m_height, renderAreaWidth, renderAreaHeight);
#else
            m_centerText
            ? Texture::ComputeOffsets(int(td.height * td.aspectRatio), td.height, viewport.m_width, viewport.m_height, renderAreaWidth, renderAreaHeight)
            : Texture::ComputeOffsets(int(td.height * td.aspectRatio), td.height, int(textHeight * aspectRatio), textHeight, int(textHeight * aspectRatio), textHeight);
#endif
        td.width -= int(2 * outlineWidth + 0.5f);
        td.height -= int(2 * outlineWidth + 0.5f);

        if (not fbo)
            RenderText(text, td.width, offset.x, offset.y, alignment, flipVertically);
        else if (fbo->Enable(-1, FBO::dbAll, true)) {
            baseRenderer.PushViewport();
            fbo->SetViewport();
            if (outlineWidth > 0) {
                offset.x -= outlineWidth / float(fbo->m_width);
                offset.y -= outlineWidth / float(fbo->m_height);
            }
            fbo->m_lastDestination = 0;
            RenderText(text, td.width, offset.x, offset.y, alignment);
            fbo->Disable();
            if (fbo->IsAvailable()) {
                if (HaveOutline())
                    RenderOutline(fbo, m_decoration);
                else if (ApplyAA())
                    AntiAlias(fbo, m_decoration.aaMethod);
            }
            baseRenderer.PopViewport();
        }
    }
}


void TextRenderer::RenderToScreen(FBO* fbo, int flipVertically) {
#if USE_TEXT_FBOS
    if (m_font)
        fbo->RenderToScreen({ .source = fbo ? fbo->GetLastDestination() : -1, .clearBuffer = false, .flipVertically = flipVertically, .scale = m_scale }, m_color); // render outline to viewport
#endif
}


void TextRenderer::Render(String text, eTextAlignments alignment, int flipVertically, int renderAreaWidth, int renderAreaHeight, bool useFBO) {
    if (m_font and (text.Length() > 0)) {
        if (not useFBO)
            RenderToBuffer(text, alignment, nullptr, baseRenderer.GetViewport(), renderAreaWidth, renderAreaHeight, flipVertically);
        else {
            FBO* fbo = GetFBO(2.0f);
            if (fbo != nullptr) {
                RenderToBuffer(text, alignment, fbo, baseRenderer.GetViewport(), renderAreaWidth, renderAreaHeight);
                RenderToScreen(fbo, flipVertically); // render outline to viewport
            }
        }
    }
}

// =================================================================================================
