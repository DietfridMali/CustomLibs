#pragma once

#include "vector.hpp"
#include "base_quadmesh.h"
#include "texture.h"
#include "rendertarget.h"
#include "mesh.h"
#include "avltree.hpp" // faster lookup than std::map
#include "colordata.h"
#include "tablesize.h"
#include "textureatlas.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26819)
#endif
#include "SDL_ttf.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define EXTERNAL_ATLAS 1

// =================================================================================================

class FontHandler {
public:
	using GlyphSize = TextureAtlas::GlyphSize;
	using TextDimensions = TextureAtlas::GlyphSize;

	struct GlyphInfo {
		Texture*	texture;
		String		name;
		int32_t		index;
		GlyphSize	glyphSize;
		Vector2f	atlasPosition;
		Vector2f	atlasSize;

		GlyphInfo(Texture* _texture = nullptr, String _name = "", int32_t _index = -1, Vector2f _position = Vector2f::ZERO,
				  Vector2f _size = Vector2f::ZERO)
			: texture(_texture)
			, name(_name)
			, index(_index)
			, atlasPosition(_position)
			, atlasSize(_size)
		{}
	};

	// One glyph on its own, in pixels at the font's size - for a caller that builds an atlas of its own
	// and does its own layout. The ink box is relative to the pen position on the baseline, y up (SDL_ttf's
	// convention); the texture RenderGlyph () makes is TTF_FontHeight () high with the baseline Ascent ()
	// rows from its top, and the pen position originX columns from its left edge.
	struct GlyphMetrics {
		int	minX{ 0 };
		int	maxX{ 0 };
		int	minY{ 0 };
		int	maxY{ 0 };
		int	advance{ 0 };
		int	width{ 0 }; // of the texture
		int	height{ 0 };
		int	originX{ 0 };
	};

private:
	TTF_Font*	m_font;
	String		m_fontName;
	int			m_fontSize;
	String		m_euroChar;
	String		m_glyphs;
	RGBAColor	m_color;
	GlyphSize	m_maxGlyphSize;
	float		m_inkTop{ 0.0f }; // start of the glyphs' actual ink band as a fraction of the (metric-padded) glyph surface height
	float		m_inkHeight{
		1.0f
	}; // height of the ink band as a fraction of the glyph surface height; 1.0 = use full surface (no metric correction)
	GfxDataLayout	m_gfxDataLayout;
	bool			m_isAvailable;

	AVLTree<String, GlyphInfo>	m_glyphDict;
	TextureAtlas				m_atlas;

public:
	static int CompareTextures(void* context, const char& key1, const char& key2);

	FontHandler();

	~FontHandler() {
		Destroy();
	}

	void Destroy(void);

	bool InitTTF(void);

	bool Create(String fontFolder, String fontName, int fontSize = 127,
				String glyphs = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-=.,*/: _?!%");

	bool Open(String fontFolder, String fontName, int fontSize);

	bool RebuildAtlas(void);

	bool SetShaping(bool rightToLeft, String script = "");

	Texture* CreateTextTexture(const String& text);

	inline GlyphInfo* FindGlyph(String key) {
		return m_glyphDict.Find(key);
	}

	static inline String NextGlyph(const String& text, int32_t& offset)
	{
		int32_t length = int32_t(text.Length());
		uint8_t c = uint8_t(text.Data()[offset]);
		int32_t glyphLength = (c < 0xC0) ? 1 : (c < 0xE0) ? 2
			: (c < 0xF0)									? 3
														  : 4;
		if (glyphLength > length - offset)
			glyphLength = length - offset;
		String glyph = text.SubStr(offset, glyphLength);
		offset += glyphLength;
		return glyph;
	}

	inline TextureAtlas& GetAtlas(void)
	noexcept
	{
		return m_atlas;
	}

	inline RenderTarget* GetRenderTarget(void)
	noexcept
	{
		return m_atlas.GetRenderTarget();
	}

	inline Texture* GetAsTexture(void)
	noexcept
	{
		return m_atlas.GetAsTexture();
	}

	inline const String& GetGlyphs(void) const
	noexcept
	{
		return m_glyphs;
	}

	inline bool IsAvailable(void)
	noexcept
	{
		return m_isAvailable;
	}

	inline float InkTop(void) const
	noexcept
	{
		return m_inkTop;
	}

	inline float InkHeight(void) const
	noexcept
	{
		return m_inkHeight;
	}

	inline int FontSize(void) const
	noexcept
	{
		return m_fontSize;
	}

	TextDimensions TextSize(String text);

	// The single glyph interface - works on a font that was only opened (Open ()), no atlas needed.
	bool HasGlyph(uint32_t codepoint);

	bool GetGlyphMetrics(uint32_t codepoint, GlyphMetrics& metrics);

	// The glyph in white on a transparent texture, metrics filled in. The caller owns the texture.
	Texture* RenderGlyph(uint32_t codepoint, GlyphMetrics& metrics);

	SDL_Surface* RenderGlyphSurface(uint32_t codepoint, GlyphMetrics& metrics);

	int Ascent(void);

	int Descent(void);

	int LineHeight(void);

private:
	Shader* LoadShader(void);

	bool InitFont(String fontFolder, String fontName, int fontSize, String glyphs);

	bool CreateTexture(const char* szChar, String key, int index);

	int CreateTextures(void);

	int BuildAtlas(void);

	bool CreateAtlas(void);

	void ComputeInkBand(void);

	bool RenderGlyphToAtlas(const String& key, GlyphInfo* info);

	bool FreeGlyph(const String& key, GlyphInfo* info);
};

// =================================================================================================
