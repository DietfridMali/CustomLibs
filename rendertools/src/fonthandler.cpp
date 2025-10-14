//pragma once

#include <algorithm>
#include "glew.h"
#include "conversions.hpp"
#include "colordata.h"
#include "fonthandler.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"
#include "tristate.h"

#ifndef _WIN32
#   include <locale>
#endif

#define USE_TEXT_FBOS 1
#define USE_ATLAS 1
#define TEST_ATLAS 0

// =================================================================================================

int FontHandler::CompareTextures(void* context, const char& key1, const char& key2) {
    return (key1 < key2) ? -1 : (key1 > key2) ? 1 : 0;
}


FontHandler::FontHandler()
    : m_font(nullptr), m_fontName(""), m_fontSize(0), m_glyphs(""), m_isAvailable(false)
{
#ifdef _WIN32
    m_euroChar = "\xE2\x82\xAC"; // "\u20AC";
#else
    //std::locale::global(std::locale("de_DE.UTF-8"));
    m_euroChar = "\u20AC";
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
#if 1 //def NDEBUG
        delete info->texture;
        info->texture = nullptr;
#endif
    }
    return true;
}


int FontHandler::BuildAtlas(void) {
    if (not m_atlas.Enable())
        return -1;
    Tristate<int> blending(-1, 0, openGLStates.SetBlending(1));
    Tristate<int> faceCulling(-1, 0, openGLStates.SetFaceCulling(0));
    baseRenderer.ResetTransformation();
    m_atlas.Initialize();
    baseRenderer.PushViewport();
    m_glyphDict.Walk(&FontHandler::RenderGlyphToAtlas, this);
    baseRenderer.PopViewport();
    m_atlas.Disable();
    openGLStates.SetBlending(blending);
    openGLStates.SetFaceCulling(faceCulling);
    return m_glyphDict.Size(); 
}


bool FontHandler::InitTTF(void) {
    static int haveTTF = 0;
    if (not haveTTF) {
        haveTTF = (0 > TTF_Init()) ? -1 : 1;
        if (haveTTF < 0) 
            fprintf(stderr, "Cannot initialize font system\n");
    }
    return haveTTF > 0;
}


bool FontHandler::InitFont(String fontFolder, String fontName, int fontSize, String glyphs) {
    if (not InitTTF())
        return false;

    if (m_font) {
        if ((fontName == fontName) and (fontSize == m_fontSize))
            return true;
        TTF_CloseFont(m_font);
        m_font = nullptr;
    }

    String fontFile = fontFolder + fontName;
    if (not (m_font = TTF_OpenFont(fontFile.Data(), fontSize))) {
        fprintf(stderr, "Cannot load font '%s'\n", (char*) fontName);
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


bool FontHandler::CreateTexture(const char* szChar, char key, int index)
{
    GlyphInfo info{ new Texture(), String (key), index };
    if (info.texture == nullptr)
        return false;
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
    info.glyphSize = GlyphSize(info.texture->GetWidth(), info.texture->GetHeight());
    m_maxGlyphSize.width = std::max(m_maxGlyphSize.width, info.texture->GetWidth());
    m_maxGlyphSize.height = std::max(m_maxGlyphSize.height, info.texture->GetHeight());
    return true;
}


int FontHandler::CreateTextures(void) {
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


bool FontHandler::CreateAtlas(void) {
    int i = CreateTextures();
    m_maxGlyphSize.Update();
    int glyphCount = m_glyphs.Length() + 1;
    m_atlas.Create("LetterAtlas", m_maxGlyphSize, m_glyphs.Length() + 1, 2);
    return BuildAtlas() == int(glyphCount);
}


FontHandler::TextDimensions FontHandler::TextSize(String text) {
    TextDimensions d;
    for (auto glyph : text) {
        GlyphInfo* info = FindGlyph(String(glyph));
        if ((info == nullptr) or (info->index < 0)) {
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
