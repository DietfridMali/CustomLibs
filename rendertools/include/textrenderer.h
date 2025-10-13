#pragma once

#include "SDL_ttf.h"

#include "vector.hpp"
#include "base_quad.h"
#include "fbo.h"
#include "avltree.hpp" // faster lookup than std::map
#include "colordata.h"
#include "tablesize.h"
#include "outlinerenderer.h"
#include "mesh.h"
#include "fonthandler.h"
#include "basesingleton.hpp"

#define EXTERNAL_ATLAS 1

// =================================================================================================

class TextRenderer 
    : public OutlineRenderer 
    , public BaseSingleton<TextRenderer>
{
public:
    using TextDecoration = OutlineRenderer::Decoration;
    using TextDimensions = FontHandler::TextDimensions;

    typedef enum {
        taLeft,
        taCenter,
        taRight
    } eTextAlignments;

private:
    RGBAColor               m_color;
    float                   m_scale;
    eTextAlignments         m_textAlignment;
    TextDecoration          m_decoration;
    VAO                     m_vao;

    Mesh                    m_mesh;
    Dictionary<int, FBO*>   m_fbos;

    FontHandler*            m_font;

public:
    static int CompareFBOs(void* context, const int& key1, const int& key2);

    TextRenderer(RGBAColor color = ColorData::White, const TextDecoration& decoration = {}, float scale = 1.0f);

    void Setup(void);

    void Fill(Vector4f color);

    void RenderToBuffer(String text, eTextAlignments alignment, FBO* fbo, Viewport& viewport, int renderAreaWidth = 0, int renderAreaHeight = 0, int flipVertically = 0);

    void RenderToScreen(FBO* fbo, int flipVertically = 0);

    void Render(String text, eTextAlignments alignment = taLeft, int flipVertically = 0, int renderAreaWidth = 0, int renderAreaHeight = 0, bool useFBO = true);

    inline FontHandler* SetFont(FontHandler * font) noexcept {
        FontHandler* currentFont = m_font;
        if ((m_font = font))
            m_mesh.SetupTexture(m_font->GetTexture());
        return currentFont;
    }

    inline FontHandler* GetFont(void) noexcept {
        return m_font;
    }

    inline TextDimensions TextSize(String text) {
        return m_font ? m_font->TextSize(text) : TextDimensions(0, 0);
    }

    inline bool SetColor(RGBAColor color = ColorData::White) noexcept {
        if (color.A() < 0.0f)
            return false;
        m_color = color;
        return true;
    }

    inline RGBAColor GetColor(void) noexcept {
        return m_color;
    }

    inline bool SetAlpha(float alpha = 1.0) noexcept {
        if (alpha < 0.0f)
            return false;
        m_color.A() = alpha;
        return true;
    }

    inline bool SetScale(float scale = 1.0) noexcept {
        if (scale < 0.0f)
            return false;
        m_scale = scale;
        return true;
    }

    void SetAAMethod(const OutlineRenderer::AAMethod& aaMethod) noexcept {
        m_decoration.aaMethod = aaMethod;
    }

    inline void SetTextAlignment(eTextAlignments alignment) noexcept {
        m_textAlignment = alignment;
    }

    inline void SetOutline(float outlineWidth = 0.0f, RGBAColor outlineColor = ColorData::Invisible) noexcept {
        m_decoration.outlineWidth = outlineWidth;
        m_decoration.outlineColor = outlineColor;
    }


    inline void SetDecoration(const TextDecoration& decoration = {}) noexcept {
        m_decoration = decoration;
    }

    inline bool HaveOutline(void) noexcept {
        return m_decoration.HaveOutline();
    }

    inline bool ApplyAA(void) noexcept {
        return m_decoration.ApplyAA();
    }

private:
    BaseQuad& CreateQuad(BaseQuad& q, float x, float y, float w, Texture* t, bool flipVertically);

    FBO* GetFBO(float scale);

    Shader* LoadShader(void);

    void RenderTextMesh(String& text, float x, float y, float scale, bool flipVertically);

    void RenderGlyphs(String& text, float x, float y, float scale, bool flipVertically);

    void RenderText(String& text, int textWidth, float xOffset, float yOffset, eTextAlignments alignment = taLeft, int flipVertically = 0);

    int SourceBuffer(bool hasOutline, bool antiAliased);

    static inline int FBOID(const int width, const int height) noexcept {
        return width << 16 | height;
    }

    static inline int FBOID(const FBO* fbo) noexcept {
        return FBOID (fbo->m_width, fbo->m_height);
    }
};

#define textRenderer TextRenderer::Instance()

// =================================================================================================


