#pragma once

#include "SDL_ttf.h"

#include "vector.hpp"
#include "base_quad.h"
#include "fbo.h"
#include "avltree.hpp" // faster lookup than std::map
#include "colordata.h"
#include "tabledimensions.h"
#include "outlinerenderer.h"
#include "mesh.h"
#include "singletonbase.hpp"

// =================================================================================================

class TextRenderer 
    : public OutlineRenderer 
    , public BaseSingleton<TextRenderer>
{
public:
    using TextDecoration = OutlineRenderer::Decoration;

    typedef enum {
        taLeft,
        taCenter,
        taRight
    } eTextAlignments;

    struct TextDimensions {
        int width = 0;
        int height = 0;
        float aspectRatio = 0.0f;

        TextDimensions(int w = 0, int h = 0)
            : width(w), height(h)
        { 
            Update();
        }

        TextDimensions& Update(void) { 
            aspectRatio = (width * height) ? float(width) / float(height) : 0.0f; 
            return *this;
        }
    };

    using GlyphSize = TextDimensions;

    struct GlyphInfo {
        Texture*                    texture;
        String                      name;
        int32_t                     index;
        GlyphSize                   glyphSize;
        Vector2f                    atlasPosition;
        Vector2f                    atlasSize;

        GlyphInfo(Texture* _texture = nullptr, String _name = "", int32_t _index = -1, Vector2f _position = Vector2f::ZERO, Vector2f _size = Vector2f::ZERO)
            : texture(_texture), name(_name), index(_index), atlasPosition(_position), atlasSize(_size)
        { }
    };

private:
    TTF_Font*                   m_font;
    String                      m_euroChar;
    String                      m_glyphs;
    bool                        m_isAvailable;
    RGBAColor                   m_color;
    float                       m_scale;
    eTextAlignments             m_textAlignment;
    struct TextDecoration       m_decoration;
    struct GlyphSize            m_maxGlyphSize;
    VAO                         m_vao;

    AVLTree<String, GlyphInfo>  m_glyphDict;
    FBO*                        m_atlas; // texture containing all letters
    Texture                     m_atlasTexture;
    Mesh                        m_mesh;
    TableDimensions             m_atlasSize;
    Dictionary<int, FBO*>       m_fbos;

public:
    static int CompareFBOs(void* context, const int& key1, const int& key2);

    static int CompareTextures(void* context, const char& key1, const char& key2);

    TextRenderer(RGBAColor color = ColorData::White, const TextDecoration& decoration = {}, float scale = 1.0f);

    void Setup(void);

    bool Create(String fontFolder, String fontName);

    void Fill(Vector4f color);

    void RenderToBuffer(String text, eTextAlignments alignment, FBO* fbo, Viewport& viewport, int renderAreaWidth = 0, int renderAreaHeight = 0, int flipVertically = 0);

    void RenderToScreen(FBO* fbo, int flipVertically = 0);

    void Render(String text, eTextAlignments alignment = taLeft, int flipVertically = 0, int renderAreaWidth = 0, int renderAreaHeight = 0, bool useFBO = true);

    inline bool SetColor(RGBAColor color = ColorData::White) {
        if (color.A() < 0.0f)
            return false;
        m_color = color;
        return true;
    }

    inline bool SetAlpha(float alpha = 1.0) {
        if (alpha < 0.0f)
            return false;
        m_color.A() = alpha;
        return true;
    }

    inline bool SetScale(float scale = 1.0) {
        if (scale < 0.0f)
            return false;
        m_scale = scale;
        return true;
    }

    void SetAAMethod(const OutlineRenderer::AAMethod& aaMethod) {
        m_decoration.aaMethod = aaMethod;
    }

    inline void SetTextAlignment(eTextAlignments alignment) {
        m_textAlignment = alignment;
    }

    inline void SetOutline(float outlineWidth = 0.0f, RGBAColor outlineColor = ColorData::Invisible) {
        m_decoration.outlineWidth = outlineWidth;
        m_decoration.outlineColor = outlineColor;
    }


    inline void SetDecoration(const TextDecoration& decoration = {}) {
        m_decoration = decoration;
    }

    inline bool HaveOutline(void) {
        return m_decoration.HaveOutline();
    }

    inline bool ApplyAA(void) {
        return m_decoration.ApplyAA();
    }

    inline GlyphInfo* FindGlyph(String key) {
        return m_glyphDict.Find(key);
    }

    struct TextDimensions TextSize(String text);

private:
    bool InitFont(String fontFolder, String fontName);

    bool CreateTexture(const char* szChar, char key, int index);

    int CreateTextures(void);

    int BuildAtlas(void);
        
    bool CreateAtlas(void);

    bool RenderGlyphToAtlas(const String& key, GlyphInfo* info);
        
    BaseQuad& CreateQuad(BaseQuad& q, float x, float y, float w, Texture* t, bool flipVertically);

    FBO* GetFBO(float scale);

    Shader* LoadShader(void);

    void RenderTextMesh(String& text, float x, float y, float scale, bool flipVertically);

    void RenderGlyphs(String& text, float x, float y, float scale, bool flipVertically);

    void RenderText(String& text, int textWidth, float xOffset, float yOffset, eTextAlignments alignment = taLeft, int flipVertically = 0);

    int SourceBuffer(bool hasOutline, bool antiAliased);

    static inline int FBOID(const int width, const int height) {
        return width << 16 | height;
    }

    static inline int FBOID(const FBO* fbo) {
        return FBOID (fbo->m_width, fbo->m_height);
    }
};

#define textRenderer TextRenderer::Instance()

// =================================================================================================


