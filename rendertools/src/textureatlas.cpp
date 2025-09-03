
#include "textureatlas.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"

// =================================================================================================

BaseQuad TextureAtlas::renderQuad;

TextureAtlas::TextureAtlas()
	: m_size(0), m_glyphSize(0), m_scale(Vector2f::ONE)
{
	renderQuad.Setup(BaseQuad::defaultVertices[BaseQuad::voZero], BaseQuad::defaultTexCoords[BaseQuad::tcRegular]);
	renderQuad.SetTransformations({ .centerOrigin = false, .autoClear = false });
}


bool TextureAtlas::Create(String name, int glyphWidth, int glyphHeight, int glyphCount, float aspectRatio, int scale) {
	m_glyphSize = TableSize(glyphWidth, glyphHeight);
	m_size.SetCols(int(ceil(sqrtf(float(glyphCount) / aspectRatio))));
	m_size.SetRows(int(ceil(float(glyphCount) / float(m_size.GetCols()))));
	if (not m_atlas.Create(m_size.GetCols() * glyphWidth, m_size.GetRows() * glyphHeight, scale, { .name = name })) {
		m_atlas.Destroy();
		return false;
	}
	m_scale = Vector2f(1.0f / float(m_size.GetCols()), 1.0f / float(m_size.GetRows()));
	return true;
}


bool TextureAtlas::Render(Shader* shader) {
	if (not shader)
		return false;
	m_atlas.Render({ .clearBuffer = false, .shader = shader });
	return true;
}


bool TextureAtlas::RenderColor(int glyphIndex, RGBAColor color) {
	return Render(baseShaderHandler.LoadPlainTextureShader(color, m_scale, GlyphOffset(glyphIndex)));
}


bool TextureAtlas::RenderGrayscale(int glyphIndex, float brightness) {
	return Render(baseShaderHandler.LoadGrayScaleShader(brightness, m_scale, GlyphOffset(glyphIndex)));
}


bool TextureAtlas::Add(Texture* glyph, int glyphIndex) {
	bool enableLocally = not m_atlas.IsEnabled();
	if (enableLocally)
		m_atlas.Enable();
	Shader* shader = baseShaderHandler.LoadPlainTextureShader(ColorData::White, m_scale, GlyphOffset(glyphIndex));
	if (shader) {
		baseRenderer.PushViewport();
		baseRenderer.SetViewport(m_size.Col(glyphIndex) * m_glyphSize.Width(), m_size.Row(glyphIndex) * m_glyphSize.Height(), m_glyphSize.Width(), m_glyphSize.Height());
		renderQuad.Render(shader, glyph, true);
		baseRenderer.PopViewport();
	}
	if (enableLocally)
		m_atlas.Disable();
	return shader != nullptr;
}

// =================================================================================================
