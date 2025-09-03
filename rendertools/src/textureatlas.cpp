
#include "textureatlas.h"
#include "base_shaderhandler.h"

// =================================================================================================

BaseQuad TextureAtlas::renderQuad;

TextureAtlas::TextureAtlas()
{
	renderQuad.Setup(BaseQuad::defaultVertices[BaseQuad::voZero], BaseQuad::defaultTexCoords[BaseQuad::tcRegular]);
	renderQuad.SetTransformations({ .centerOrigin = false, .autoClear = false });
}


bool TextureAtlas::Create(String name, int glyphWidth, int glyphHeight, int glyphCount, float aspectRatio, float scale) {
	m_size.SetCols(int(ceil(sqrtf(float(glyphCount) / m_maxGlyphSize.aspectRatio))));
	m_size.SetRows(int(ceil(float(glyphCount) / float(m_atlasSize.GetCols()))));
	if (not m_atlas.Create(name, m_size.GetCols() * glyphWidth, m_size.GetRows() * glyphHeight, scale, { .name = name })) {
		m_atlas.Destroy();
		return false;
	}
	m_tcScale = Vector2f(1.0f / float(m_size.GetCols()), 1.0f / float(m_size.GetRows()));
}


bool TextureAtlas::Render(int glyphIndex, RGBAColor color, bool grayscale) {
	Vector2f offset = Vector2f(m_size.Colf(glyphIndex) * m_tcScale.X(), m_size.Rowf(glyphIndex) * m_tcScale.Y());
	Shader* shader = 
		grayscale
		? baseShaderHandler.LoadGrayScaleShader(color, m_scale, offset)
		: baseShaderHandler.LoadPlainTextureShader(color, m_scale, offset);
	if (shader)
		m_atlas.Render({ .clearBuffer = false, .shader = shader });
	return false;
}


bool TextureAtlas::Add(Texture* glyph, int glyphIndex) {
	m_atlas.Enable();
	m_atlas.SetViewport();
	Shader* shader = LoadPlainTextureShader(color, m_scale, offset);
	bool added = renderQuad.Render(shader, glyph, true);
	m_atlas.Disable();
	return added;
}

// =================================================================================================
