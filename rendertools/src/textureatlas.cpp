
#include "textureatlas.h"

// =================================================================================================

bool TextureAtlas::Create(String name, int glyphWidth, int glyphHeight, int glyphCount, float aspectRatio, float scale) {
	m_size.SetCols(int(ceil(sqrtf(float(glyphCount) / m_maxGlyphSize.aspectRatio))));
	m_size.SetRows(int(ceil(float(glyphCount) / float(m_atlasSize.GetCols()))));
	return m_atlas.Create(name, m_size.GetCols() * glyphWidth, m_size.GetRows() * glyphHeight, scale, { .name = name })
}

bool TextureAtlas::Render(int glyphIndex) {
	return false;
}

// =================================================================================================
