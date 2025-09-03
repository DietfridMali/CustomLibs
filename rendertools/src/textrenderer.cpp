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

// =================================================================================================

int TextRenderer::CompareFBOs(void* context, const int& key1, const int& key2) {
    return (key1 < key2) ? -1 : (key1 > key2) ? 1 : 0;
}


int TextRenderer::CompareTextures(void* context, const char& key1, const char& key2) {
    return (key1 < key2) ? -1 : (key1 > key2) ? 1 : 0;
}


TextRenderer::TextRenderer(RGBAColor color, const TextDecoration& decoration, float scale)
    : m_color(color), m_scale(scale), m_font(nullptr), m_textAlignment(taCenter), m_decoration(decoration), m_isAvailable(false)
{
    Setup();
}


bool TextRenderer::RenderGlyphToAtlas(const String& key, GlyphInfo* info) {
    Shader* shader = LoadShader();
    if (not shader)
        return false;
    if (info) {
        // compute position and size relative to atlas dimensions; the grid size is determined by m_maxGlyphSize / (atlasWidth, atlasHeight)
        info->glyphSize.width = info->texture->GetWidth();
        info->glyphSize.height = info->texture->GetHeight();
        float colScale = 1.0f / float(m_atlasSize.GetCols());
        float rowScale = 1.0f / float(m_atlasSize.GetRows());
        int x = info->index % m_atlasSize.GetCols();
        int y = info->index / m_atlasSize.GetCols();
        // base position scaled by atlas color buffer dimensions
        info->atlasPosition = Vector2f(float(x) * colScale, float(y) * rowScale);
        // actual width and height scaled by atlas color buffer dimensions
        info->atlasSize = { float(info->glyphSize.width) / float(m_maxGlyphSize.width) * colScale, float(info->glyphSize.height) / float(m_maxGlyphSize.height) * rowScale };
        float dx = info->atlasSize.X();
        float dy = info->atlasSize.Y();
        Vector3f p(info->atlasPosition.X(), info->atlasPosition.Y(), 0.0);
        BaseQuad q;
        q.Setup({ p, p + Vector3f(0.0f, dy, 0.0f), p + Vector3f(dx, dy, 0.0f), p + Vector3f(dx, 0.0f, 0.0f) },
                { TexCoord{ 0, 0 }, TexCoord{ 0, 1 }, TexCoord{ 1, 1 }, TexCoord{ 1, 0 } },
                info->texture,
                ColorData::White);
        //info->atlasPosition.Y() = 1.0f - info->atlasPosition.Y();
#if 1
        q.Render(shader, info->texture, ColorData::White);
#else
        if (info->index & 1)
            q.Fill(RGBAColor(p.X(), p.Y(), 0.0f, 1.0f));
        else
            q.Fill(RGBAColor(p.Y(), p.X(), 0.0f, 1.0f));
#endif
#if USE_ATLAS
        delete info->texture;
        info->texture = nullptr;
#endif
        //++glyphCount;
    }
    return true;
}


int TextRenderer::BuildAtlas(void) {
    if (not m_atlas->Enable())
        return -1;
    m_atlas->SetViewport();
    baseRenderer.ResetTransformation();
#if 1
    m_glyphDict.Walk(&TextRenderer::RenderGlyphToAtlas, this);
#endif
    m_atlas->Disable();
    m_atlasTexture.m_handle = m_atlas->BufferHandle(0);
    m_atlasTexture.HasBuffer() = true;
    m_mesh.SetDynamic(true);
    m_mesh.Init(GL_QUADS, 0, &m_atlasTexture);

    return m_glyphDict.Size(); // glyphCount;
}


void TextRenderer::Setup(void) {
#ifdef _WIN32
    m_euroChar = "\xE2\x82\xAC"; // "\u20AC";
#else
    //std::locale::global(std::locale("de_DE.UTF-8"));
    m_euroChar = "\u20AC";
#endif
    m_glyphs = String("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-=.,*/: _?!%"); // +m_euroChar;
#if 1 //!(USE_STD || USE_STD_MAP)
    //m_fbos.SetComparator(TextRenderer::CompareFBOs);
    m_glyphDict.SetComparator(String::Compare); //TextRenderer::CompareTextures);
#endif
}


bool TextRenderer::InitFont(String fontFolder, String fontName) {
    if (0 > TTF_Init()) {
        fprintf(stderr, "Cannot initialize font system\n");
        return false;
    }
    String fontFile = fontFolder + fontName;
    if (not (m_font = TTF_OpenFont(fontFile.Data(), 120))) {
        fprintf(stderr, "Cannot load font '%s'\n", (char*) fontName);
        return false;
    }
    return true;
}


bool TextRenderer::Create(String fontFolder, String fontName) {
    if (m_isAvailable = InitFont(fontFolder, fontName))
        CreateAtlas();
    return m_isAvailable;
}


bool TextRenderer::CreateTexture(const char* szChar, char key, int index)
{
    GlyphInfo info{ new Texture(), String (key), index };
    if (info.texture == nullptr)
        return false;
    info.texture = new Texture();
    SDL_Surface* surface = (strlen(szChar) == 1)
        ? TTF_RenderText_Solid(m_font, szChar, SDL_Color(255, 255, 255, 255))
        : TTF_RenderUTF8_Solid(m_font, szChar, SDL_Color(255, 255, 255, 255));
    if (not info.texture->CreateFromSurface(surface)) {
        delete info.texture;
        info = GlyphInfo();
        return false;
        }
    info.texture->Deploy();
    m_glyphDict.Insert(String(key), info);
    m_maxGlyphSize.width = std::max(m_maxGlyphSize.width, info.texture->GetWidth());
    m_maxGlyphSize.height = std::max(m_maxGlyphSize.height, info.texture->GetHeight());
    return true;
}


int TextRenderer::CreateTextures(void) {
    char szChar[4] = " ";
    int32_t i = 0;
    for (char* info = m_glyphs.Data(); *info; info++) {
        szChar[0] = *info;
        if (CreateTexture(szChar, *info, i))
            ++i;
    }
    if (CreateTexture((const char*)m_euroChar, '€', i))
        ++i;
    return i;
}


bool TextRenderer::CreateAtlas(void) {
    int i = CreateTextures();
    m_maxGlyphSize.Update();
    float l = float(m_glyphs.Length() + 1);
    m_atlasSize.SetCols(int(ceil(sqrtf(l / m_maxGlyphSize.aspectRatio))));
    m_atlasSize.SetRows(int(ceil(l / float(m_atlasSize.GetCols()))));
    int w = m_atlasSize.GetCols() * m_maxGlyphSize.width;
    int h = m_atlasSize.GetRows() * m_maxGlyphSize.height;
    m_atlas = new FBO();
    if (not m_atlas)
        return false;
    if (not m_atlas->Create(w, h, 2, { .name = "GlyphAtlas" }))
        return false;
    return BuildAtlas() == int(l);
}


struct TextRenderer::TextDimensions TextRenderer::TextSize(String text) {
    TextDimensions d;
    for (auto glyph : text) {
        GlyphInfo* info = FindGlyph(String(glyph));
        if (info->index < 0) {
            fprintf(stderr, "texture '%c' not found\r\n", glyph);
            return TextDimensions();
        }
        int tw = info->glyphSize.width;
        d.width += tw;
        //auto Max = [=](auto& a, auto b) { return (a > b) ? a : b; };
        int th = info->glyphSize.height;
        if (d.height < th)
            d.height = th;
        }
    return d.Update();
}


FBO* TextRenderer::GetFBO(float scale) {
    FBO** fboRef = m_fbos.Find(FBOID(baseRenderer.Viewport().m_width, baseRenderer.Viewport().m_height));
    if (fboRef != nullptr)
        return *fboRef;
    FBO* fbo = new FBO();
    fbo->Create(baseRenderer.Viewport().m_width, baseRenderer.Viewport().m_height, 2, {.name = "text", .colorBufferCount = 2});
    m_fbos.Insert(FBOID(fbo), fbo);
    return fbo;
}


Shader* TextRenderer::LoadShader(void) {
    static ShaderLocationTable locations;
    Shader* shader = baseShaderHandler.SetupShader("plainTexture");
    if (shader) {
        locations.Start();
        shader->SetVector4f("surfaceColor", locations.Current(), m_color);
        shader->SetVector2f("tcOffset", locations.Current(), Vector2f::ZERO);
        shader->SetVector2f("tcScale", locations.Current(), Vector2f::ONE);
        //shader->SetFloat("premultiply", locations.Current(), 0.0f);
    }
    return shader;
}


void TextRenderer::RenderTextMesh(String& text, float x, float y, float scale, bool flipVertically) {
    Shader* shader = LoadShader();
    if (not shader)
        return;

    // test code: display entire atlas on screen
#if TEST_ATLAS
    baseRenderer.SetViewport(::Viewport (0, 0, m_atlas->GetWidth(), m_atlas->GetHeight()));
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    baseRenderer.ClearGLError();
    m_atlas->RenderToScreen({}, ColorData::Yellow);
    baseRenderer.CheckGLError("render glyph atlas");
    return;
#endif

    if (flipVertically)
        y = -y;

    m_mesh.ResetVAO();
    for (auto glyph : text) {
        GlyphInfo* info = FindGlyph(String(glyph));

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
    m_mesh.Render(shader, &m_atlasTexture);
}


BaseQuad& TextRenderer::CreateQuad(BaseQuad& q, float x, float y, float w, Texture* t, bool flipVertically) {
    if (flipVertically)
        q.Setup({ Vector3f{x, y, 0.0}, Vector3f{x + w, y, 0.0}, Vector3f{x + w, -y, 0.0}, Vector3f{x, -y, 0.0} },
            { TexCoord{0, 1}, TexCoord{1, 1}, TexCoord{1, 0}, TexCoord{0, 0} },
            t, m_color);
    else
        q.Setup({ Vector3f{x, y, 0.0}, Vector3f{x + w, y, 0.0}, Vector3f{x + w, -y, 0.0}, Vector3f{x, -y, 0.0} },
            { TexCoord{ 0, 0 }, TexCoord{ 1, 0 }, TexCoord{ 1, 1 }, TexCoord{ 0, 1 } },
            t, m_color);
    return q;
}


void TextRenderer::RenderGlyphs(String& text, float x, float y, float scale, bool flipVertically) {
    Shader* shader = LoadShader();
    BaseQuad q;
    for (auto glyph : text) {
        GlyphInfo* info = FindGlyph(String(glyph));
        if (info->index < 0)
            fprintf(stderr, "texture '%c' not found\r\n", glyph);
        else {
            float width = float(info->glyphSize.width) * scale;
            CreateQuad(q, x, y, width, info->texture, flipVertically);
#if 1
            q.Render(shader, info->texture, true);
#else
            q.Fill((i & 1) ? ColorData::Orange : ColorData::MediumBlue);
#endif
            //q.Render(m_color, m_color.A());
            x += width;
        }
    }
}


void TextRenderer::RenderText(String& text, int textWidth, float xOffset, float yOffset, eTextAlignments alignment, int flipVertically) {
    baseRenderer.PushMatrix();
#if !TEST_ATLAS
    baseRenderer.ResetTransformation();
    baseRenderer.Translate(0.5f, 0.5f, 0.0f);
#endif
    Tristate<GLenum> depthFunc (GL_NONE, GL_LEQUAL, openGLStates.DepthFunc(GL_ALWAYS));
    float letterScale = 2 * xOffset / float(textWidth);
    switch (alignment) {
    case taLeft:
        xOffset = -0.5f;
        break;
    case taRight:
        xOffset = 1.0f - xOffset * 2;
        break;
    case taCenter:
    default:
        xOffset = -xOffset;
    }
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
    if (m_isAvailable) {
        if (fbo)
            fbo->m_name = String::Concat ("[", text, "]");
        auto [textWidth, textHeight, aspectRatio] = TextSize(text);
        float outlineWidth = m_decoration.outlineWidth * 2;
        textWidth += int(2 * outlineWidth + 0.5f);
        textHeight += int(2 * outlineWidth + 0.5f);
        tRenderOffsets offset =
#if 1
            Texture::ComputeOffsets(int(textHeight * aspectRatio), textHeight, viewport.m_width, viewport.m_height, renderAreaWidth, renderAreaHeight);
#else
            m_centerText
            ? Texture::ComputeOffsets(int(textHeight * aspectRatio), textHeight, viewport.m_width, viewport.m_height, renderAreaWidth, renderAreaHeight)
            : Texture::ComputeOffsets(int(textHeight * aspectRatio), textHeight, int(textHeight * aspectRatio), textHeight, int(textHeight * aspectRatio), textHeight);
#endif
        textWidth -= int(2 * outlineWidth + 0.5f);
        textHeight -= int(2 * outlineWidth + 0.5f);

        if (not fbo)
            RenderText(text, textWidth, offset.x, offset.y, alignment, flipVertically);
        else if (fbo->Enable(-1, FBO::dbAll, true)) {
            baseRenderer.PushViewport();
            fbo->SetViewport();
            if (outlineWidth > 0) {
                offset.x -= outlineWidth / float(fbo->m_width);
                offset.y -= outlineWidth / float(fbo->m_height);
            }
            fbo->m_lastDestination = 0;
            RenderText(text, textWidth, offset.x, offset.y, alignment);
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
    if (m_isAvailable)
        fbo->RenderToScreen({ .source = fbo ? fbo->GetLastDestination() : -1, .clearBuffer = false, .flipVertically = flipVertically, .scale = m_scale }, m_color); // render outline to viewport
#endif
}


void TextRenderer::Render(String text, eTextAlignments alignment, int flipVertically, int renderAreaWidth, int renderAreaHeight, bool useFBO) {
    if (m_isAvailable) {
        if (useFBO) {
            RenderToBuffer(text, alignment, nullptr, baseRenderer.Viewport(), renderAreaWidth, renderAreaHeight, flipVertically);
        }
        else {
            FBO* fbo = GetFBO(2.0f);
            if (fbo != nullptr) {
                RenderToBuffer(text, alignment, fbo, baseRenderer.Viewport(), renderAreaWidth, renderAreaHeight);
                RenderToScreen(fbo, flipVertically); // render outline to viewport
            }
        }
    }
}

// =================================================================================================
