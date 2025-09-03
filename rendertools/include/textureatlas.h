#pragma once

#include "vector.hpp"
#include "tablesize.h"
#include "texture.h"
#include "fbo.h"
#include "colordata.h"

// =================================================================================================

class TextureAtlas {
protected:
	FBO			m_atlas;
	TableSize	m_size;
	Vector2f	m_scale;

	static BaseQuad	renderQuad;

public:
	TextureAtlas()
		: m_size(0), m_scale(Vector2f::ONE)
	{ }

	~TextureAtlas() = default;

	inline Vector2f GlyphOffset(int glyphIndex) {
		return 
			(m_scale.X() * m_scale.Y()) // both != 0?
			? Vector2f(m_size.Colf(glyphIndex) * m_scale.X(), m_size.Rowf(glyphIndex) * m_scale.Y())
			: Vector2f::ZERO;
	}

	bool Create(String name, int glyphWidth, int glyphHeight, int glyphCount, float aspectRatio = 1.0f, float scale = 1);

	bool Render(int glyphIndex, RGBAColor color, bool grayscale = false);

	bool Add(Texture* glyph, int glyphIndex);
};

// =================================================================================================
