#pragma once

#include "vector.hpp"
#include "base_quad.h"
#include "texture.h"
#include "fbo.h"
#include "mesh.h"
#include "avltree.hpp" // faster lookup than std::map
#include "colordata.h"
#include "tablesize.h"
#include "textureatlas.h"
#include "SDL_ttf.h"

#define EXTERNAL_ATLAS 1

// =================================================================================================

class FontHandler 
{
public:
    using GlyphSize = TextureAtlas::GlyphSize;
    using TextDimensions = TextureAtlas::GlyphSize;

    struct GlyphInfo {
        Texture*                texture;
        String                  name;
        int32_t                 index;
        GlyphSize               glyphSize;
        Vector2f                atlasPosition;
        Vector2f                atlasSize;

        GlyphInfo(Texture* _texture = nullptr, String _name = "", int32_t _index = -1, Vector2f _position = Vector2f::ZERO, Vector2f _size = Vector2f::ZERO)
            : texture(_texture), name(_name), index(_index), atlasPosition(_position), atlasSize(_size)
        { }
    };

private:
    TTF_Font*                   m_font;
    String                      m_fontName;
    int                         m_fontSize;
    String                      m_euroChar;
    String                      m_glyphs;
    RGBAColor                   m_color;
    GlyphSize                   m_maxGlyphSize;
    VAO                         m_vao;
    bool                        m_isAvailable;

    AVLTree<String, GlyphInfo>  m_glyphDict;
    TextureAtlas                m_atlas;

public:
    static int CompareTextures(void* context, const char& key1, const char& key2);

    FontHandler();

    ~FontHandler() {
        Destroy();
    }

    void Destroy(void);

    bool InitTTF(void);

    bool Create(String fontFolder, String fontName, int fontSize = 127, String glyphs = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-=.,*/: _?!%");

    inline GlyphInfo* FindGlyph(String key) {
        return m_glyphDict.Find(key);
    }

    inline TextureAtlas& GetAtlas(void) noexcept {
        return m_atlas;
    }

    inline FBO* GetFBO(void) noexcept {
        return m_atlas.GetFBO();
    }

    inline Texture* GetTexture(void) noexcept {
        return m_atlas.GetTexture();
    }

    inline const String& GetGlyphs(void) const noexcept {
        return m_glyphs;
    }

    inline bool IsAvailable(void) noexcept {
        return m_isAvailable;
    }

    TextDimensions TextSize(String text);

private:
    Shader* LoadShader(void);

    bool InitFont(String fontFolder, String fontName, int fontSize, String glyphs);

    bool CreateTexture(const char* szChar, char key, int index);

    int CreateTextures(void);

    int BuildAtlas(void);
        
    bool CreateAtlas(void);

    bool RenderGlyphToAtlas(const String& key, GlyphInfo* info);
};

// =================================================================================================


