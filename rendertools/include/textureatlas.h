#pragma once

#include "vector.hpp"
#include "tablesize.h"
#include "texture.h"
#include "fbo.h"
#include "colordata.h"

// =================================================================================================

class TextureAtlas {
public:
	struct GlyphSize {
		int width = 0;
		int height = 0;
		float aspectRatio = 0.0f;

		GlyphSize(int w = 0, int h = 0)
			: width(w), height(h)
		{
			Update();
		}

		GlyphSize& Update(void) {
			aspectRatio = (width * height) ? float(width) / float(height) : 1.0f;
			return *this;
		}
#if 0
		GlyphSize& operator=(const GlyphSize& other) {
			width = other.width;
			height = other.height;
			aspectRatio = other.aspectRatio;
			return *this;
		}
#endif
	};

protected:
	FBO			m_atlas;
	Texture		m_texture;
	TableSize	m_size;
	GlyphSize	m_glyphSize;
	Vector2f	m_scale;

	static BaseQuad	renderQuad;

public:
	TextureAtlas();

	~TextureAtlas() = default;

	inline Vector2f GlyphOffset(int glyphIndex) {
		return
			(m_scale.X() * m_scale.Y()) // both != 0?
			? Vector2f(m_size.Colf(glyphIndex) * m_scale.X(), m_size.Rowf(glyphIndex) * m_scale.Y())
			: Vector2f::ZERO;
	}

	inline Vector2f GlyphScale(void) noexcept {
		return m_scale;
	}

	bool Create(String name, GlyphSize glyphSize, int glyphCount, int scale = 1);

	bool Render(Shader* shader);

	bool RenderColored(int glyphIndex, RGBAColor color = ColorData::White);

	bool RenderGrayscale(int glyphIndex, float brightness = 1.0f);

	bool Add(Texture* glyph, int glyphIndex);

	Texture& GetTexture(void) noexcept {
		return m_texture;
	}

	inline bool Enable(void) {
		return m_atlas.Enable();
	}

	inline void Disable(void) {
		m_atlas.Disable();
	}

	inline void SetViewport(void) {
		m_atlas.SetViewport();
	}

	inline TableSize& Size(void) {
		return m_size;
	}
	 
	inline int GetWidth(bool scaled = false) noexcept {
		return m_atlas.GetWidth(scaled);
	}

	inline int GetHeight(bool scaled = false) noexcept {
		return m_atlas.GetHeight(scaled);
	}

	inline FBO& GetFBO(void) noexcept {
		return m_atlas;
	}
};

// =================================================================================================
