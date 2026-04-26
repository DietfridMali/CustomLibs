//pragma once

#include <algorithm>
#include "conversions.hpp"
#include "colordata.h"
#include "fonthandler.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"

#ifndef _WIN32
#   include <locale>
#endif

#define USE_TEXT_RTS 1
#define USE_ATLAS 1
#define TEST_ATLAS 0

// =================================================================================================

int FontHandler::CompareTextures(void* context, const char& key1, const char& key2) {
    return (key1 < key2) ? -1 : (key1 > key2) ? 1 : 0;
}


FontHandler::FontHandler()
    : m_font(nullptr), m_fontName(""), m_fontSize(0), m_glyphs(""), m_isAvailable(false)
{
    m_euroChar = "\xE2\x82\xAC"; // "\u20AC";
#if 0
#ifdef _WIN32
    m_euroChar = "\xE2\x82\xAC"; // "\u20AC";
#else
    //std::locale::global(std::locale("de_DE.UTF-8"));
    m_euroChar = "\u20AC";
#endif
#endif
    m_glyphDict.SetComparator(String::Compare); //FontHandler::CompareTextures);
}


Shader* FontHandler::LoadShader(void) {
    return baseShaderHandler.LoadPlainTextureShader(ColorData::White);
}

bool FontHandler::RenderGlyphToAtlas(const String& key, GlyphInfo* info) {
    Shader* shader = LoadShader();
    if (not shader)
        return false;
    if (info) {
        info->glyphSize.width = info->texture->GetWidth();
        info->glyphSize.height = info->texture->GetHeight();
        info->atlasPosition = m_atlas.GlyphOffset(info->index);
        Vector2f scale = Vector2f(float(info->glyphSize.width) / float(m_maxGlyphSize.width), float(info->glyphSize.height) / float(m_maxGlyphSize.height));
        // compute position and size relative to atlas dimensions; the grid size is determined by m_maxGlyphSize / (atlasWidth, atlasHeight)
        info->atlasSize = scale * m_atlas.GlyphScale();
        m_atlas.Add(info->texture, info->index, scale);
        delete info->texture;
        info->texture = nullptr;
    }
#ifdef _DEBUG
    else
        fprintf(stderr, "unknown glyph\n");
#endif
    return true;
}


// FontHandler is gfx api agnostic, but for directx, textures must be kept until the command list used to render the glyphs 
// to the atlas has been executed.  So we keep the textures in memory until the atlas is built, then free them.
bool FontHandler::FreeGlyph(const String& key, GlyphInfo* info) { 
    if (info) {
        delete info->texture;
        info->texture = nullptr;
    }
    return true;
}


int FontHandler::BuildAtlas(void) {
#if 0
    m_atlas.GetRenderTarget()->SetClearColor(RGBAColor(0.5, 0, 0.5, 0));
#endif
    if (not m_atlas.Enable())
        return -1;
    gfxStates.SetBlending(0);
    gfxStates.SetFaceCulling(0);
    gfxStates.SetDepthTest(0);
    gfxStates.SetDepthWrite(0);
    baseRenderer.ResetTransformation();
    m_atlas.Initialize();
    baseRenderer.PushViewport();
    m_glyphDict.Walk(&FontHandler::RenderGlyphToAtlas, this);
    baseRenderer.PopViewport();
    m_atlas.Disable(true);
    return m_glyphDict.Size();
}


bool FontHandler::InitTTF(void) {
    static int haveTTF = 0;
    if (not haveTTF) {
        haveTTF = (0 > TTF_Init()) ? -1 : 1;
        if (haveTTF < 0) 
            fprintf(stderr, "Smiley-Battle: Cannot initialize font system.\n");
    }
    return haveTTF > 0;
}


bool FontHandler::InitFont(String fontFolder, String fontName, int fontSize, String glyphs) {
    if (not InitTTF())
        return false;

    if (m_font) {
        if ((fontName == m_fontName) and (fontSize == m_fontSize))
            return true;
        TTF_CloseFont(m_font);
        m_font = nullptr;
    }

    String fontFile = fontFolder + fontName;
    if (not (m_font = TTF_OpenFont(fontFile.Data(), fontSize))) {
        fprintf(stderr, "Smiley-Battle: Cannot load font '%s'(%s).\n", (char*) fontName, TTF_GetError());
        return false;
    }
    //SDL_Log("family=%s style=%s", TTF_FontFaceFamilyName(m_font), TTF_FontFaceStyleName(m_font));
    m_glyphs = glyphs;
    m_fontName = fontName;
    m_fontSize = fontSize;
    return true;
}


bool FontHandler::Create(String fontFolder, String fontName, int fontSize, String glyphs) {
    m_isAvailable = InitFont(fontFolder, fontName, fontSize, glyphs) and CreateAtlas();
    return m_isAvailable;
}


bool FontHandler::CreateTexture(const char* szChar, String key, int index)
{
    GlyphInfo info{ new Texture(), String (key), index };
    if (info.texture == nullptr)
        return false;
    SDL_Surface* surface = (strlen(szChar) == 1)
        ? TTF_RenderText_Solid(m_font, szChar, SDL_Color(255, 255, 255, 255))
        : TTF_RenderUTF8_Solid(m_font, szChar, SDL_Color(255, 255, 255, 255));
    if ((surface == nullptr) or not info.texture->CreateFromSurface(surface, { .isDisposable = true })) {
        delete info.texture;
        if (surface)
			SDL_FreeSurface(surface);
        info = GlyphInfo();
        return false;
        }
#if 0 // macht jetzt Texture::CreateFromSurface
    info.texture->Deploy();
#endif
    if (not m_glyphDict.Insert(info.name, info))
        return false;
    info.glyphSize = GlyphSize(info.texture->GetWidth(), info.texture->GetHeight());
    m_maxGlyphSize.width = std::max(m_maxGlyphSize.width, info.texture->GetWidth());
    m_maxGlyphSize.height = std::max(m_maxGlyphSize.height, info.texture->GetHeight());
    return true;
}


int FontHandler::CreateTextures(void) {
    char szChar[4] = " ";
    int32_t i = 0;
    void* cl = baseRenderer.StartOperation("FontHandler::CreateTextures");
    for (char* info = m_glyphs.Data(); *info; info++) {
        szChar[0] = *info;
        if (CreateTexture(szChar, String(*info), i))
            ++i;
    }
    if (CreateTexture((const char*)m_euroChar, String(m_euroChar), i))
        ++i;
    baseRenderer.FinishOperation(cl, true);
    return i;
}


bool FontHandler::CreateAtlas(void) {
    int i = CreateTextures();
    m_maxGlyphSize.Update();
    int glyphCount = m_glyphs.Length() + 1;
    if (not m_atlas.Create("LetterAtlas", m_maxGlyphSize, m_glyphs.Length() + 1, 2)) {
#ifdef _DEBUG
        fprintf(stderr, "FontHandler: Failed to create atlas.\n");
#endif
        return false;
    }
#ifdef _DEBUG
    if (BuildAtlas() == int(glyphCount))
        return true;
    fprintf(stderr, "FontHandler: Failed to create all glyphs.\n");
    return false;
#else
    return BuildAtlas() == int(glyphCount);
#endif
}


FontHandler::TextDimensions FontHandler::TextSize(String text) {
    TextDimensions d;
    for (auto glyph : text) {
        GlyphInfo* info = FindGlyph(String(glyph));
        if ((info == nullptr) or (info->index < 0)) {
#ifdef _DEBUG
            fprintf(stderr, "Couldn't load texture for glyph '%c'\r\n", glyph);
#endif
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


void FontHandler::Destroy(void) {
    m_atlas.Destroy();
    if (m_font) {
        TTF_CloseFont(m_font);
        m_font = nullptr;
        m_fontName = "";
        m_fontSize = 0;
    }
}


// =================================================================================================
