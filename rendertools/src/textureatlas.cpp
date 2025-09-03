
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

// condition: all glyphs must fit into glyphWidth, glyphHeight - that's the grid they will be rendered into
bool TextureAtlas::Create(String name, GlyphSize glyphSize, int glyphCount, int scale) {
	m_glyphSize = glyphSize;
	m_size.SetCols(int(ceil(sqrtf(float(glyphCount) / glyphSize.aspectRatio))));
	m_size.SetRows(int(ceil(float(glyphCount) / float(m_size.GetCols()))));
	if (not m_atlas.Create(m_size.GetCols() * glyphSize.width, m_size.GetRows() * glyphSize.height, scale, { .name = name })) {
		m_atlas.Destroy();
		return false;
	}
	m_scale = Vector2f(1.0f / float(m_size.GetCols()), 1.0f / float(m_size.GetRows()));
	m_texture.m_handle = m_atlas.BufferHandle(0);
	m_texture.HasBuffer() = true;

	return true;
}


bool TextureAtlas::Render(Shader* shader) {
	if (not shader)
		return false;
	m_atlas.Render({ .clearBuffer = false, .shader = shader });
	return true;
}


bool TextureAtlas::RenderColored(int glyphIndex, RGBAColor color) {
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
		baseRenderer.SetViewport(m_size.Col(glyphIndex) * m_glyphSize.width, m_size.Row(glyphIndex) * m_glyphSize.height, m_glyphSize.width, m_glyphSize.height);
		renderQuad.Render(shader, glyph, true);
		baseRenderer.PopViewport();
	}
	if (enableLocally)
		m_atlas.Disable();
	return shader != nullptr;
}

// =================================================================================================
