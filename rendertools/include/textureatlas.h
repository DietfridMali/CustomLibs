#pragma once

#include "tablesize.h"
#include "texture.h"
#include "fbo.h"

// =================================================================================================

class TextureAtlas {
protected:
	FBO			m_atlas;
	TableSize	m_size;

	TextureAtlas()
		: m_size(0)
	{ }

	~TextureAtlas() = default;

	bool Create(String name, int glyphWidth, int glyphHeight, int glyphCount, float aspectRatio = 1.0f, float scale = 1);

	bool Render(int glyphIndex);
};

// =================================================================================================


