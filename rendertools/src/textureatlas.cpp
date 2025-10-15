
#include "textureatlas.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"

// =================================================================================================

BaseQuad TextureAtlas::renderQuad;

TextureAtlas::TextureAtlas()
	: _m_atlas(nullptr)
	, m_size(0)
	, m_glyphSize(0)
	, m_scale(Vector2f::ONE)
{
}


void TextureAtlas::Initialize(void) {
	renderQuad.Setup(BaseQuad::defaultVertices[BaseQuad::voZero], BaseQuad::defaultTexCoords[BaseQuad::tcRegular], true);
	renderQuad.SetTransformations({ .centerOrigin = false, .autoClear = false });
}

// condition: all glyphs must fit into glyphWidth, glyphHeight - that's the grid they will be rendered into
bool TextureAtlas::Create(String name, GlyphSize glyphSize, int glyphCount, int scale) {
	if (m_atlas)
		delete m_atlas;
	m_atlas = new FBO();
	if (not m_atlas)
		return false;
	m_glyphSize = glyphSize;
	m_size.SetCols(int(ceil(sqrtf(float(glyphCount) / glyphSize.aspectRatio))));
	m_size.SetRows(int(ceil(float(glyphCount) / float(m_size.GetCols()))));
	if (not m_atlas->Create(m_size.GetCols() * glyphSize.width, m_size.GetRows() * glyphSize.height, scale, { .name = name })) {
		m_atlas->Destroy();
		return false;
	}
	m_scale = Vector2f(1.0f / float(m_size.GetCols()), 1.0f / float(m_size.GetRows()));
	GetTexture()->m_handle = m_atlas->BufferHandle(0);
	GetTexture()->HasBuffer() = true;
	GetTexture()->SetParams(true);
	return true;
}


bool TextureAtlas::Render(Shader* shader) {
	if (not (m_atlas and shader))
		return false;
	m_atlas->Render({ .clearBuffer = false, .centerOrigin = true, .shader = shader });
	return true;
}


bool TextureAtlas::RenderColored(int glyphIndex, RGBAColor color) {
	return Render(baseShaderHandler.LoadPlainTextureShader(color, GlyphOffset(glyphIndex), m_scale));
}


bool TextureAtlas::RenderGrayscale(int glyphIndex, float brightness) {
	return Render(baseShaderHandler.LoadGrayscaleShader(brightness, GlyphOffset(glyphIndex), m_scale));
}


bool TextureAtlas::Add(Texture* glyph, int glyphIndex, Vector2f& scale) {
	if (not m_atlas)
		return false;
	bool enableLocally = not m_atlas->IsEnabled();
	if (enableLocally) {
		baseRenderer.PushViewport();
		m_atlas->Enable();
	}
	int x = m_size.Col(glyphIndex);
	int y = m_size.Row(glyphIndex);
	int l, t, w, h;
#if 0
	Vector2f offset{ GlyphOffset(glyphIndex) };
	Vector2f size{ float(m_atlas->GetWidth(true)), float(m_atlas->GetHeight(true)) };
	l = int(roundf(offset.X() * size.X()));
	t = int(roundf(offset.Y() * size.Y()));
	w = int(roundf(m_scale.X() * size.X()));
	h = int(roundf(m_scale.Y() * size.Y()));
#else
	int s = m_atlas->GetScale();
	w = m_glyphSize.width * s;
	h = m_glyphSize.height * s;
	l = x * w;
	t = y * h;
#endif
	baseRenderer.SetViewport(Viewport(l, t, int(roundf(scale.X() * w)), int(roundf(scale.Y() * h))), 0, 0, true, true);
	Shader* shader = baseShaderHandler.LoadPlainTextureShader(ColorData::White); // , GlyphOffset(glyphIndex), m_scale);
	if (shader) {
		float c = float(glyphIndex) / float(m_size.GetSize());
#if 1
		//renderQuad.SetTransformations({ .centerOrigin = true, .flipVertically = true, .rotation = 0.0f });
		renderQuad.Render(shader, glyph, true);
#else
		renderQuad.Fill(RGBAColor(c, c, c, 1)); 
#endif
	}

	if (enableLocally) {
		m_atlas->Disable();
		baseRenderer.PopViewport();
	}
	return shader != nullptr;
}

// =================================================================================================
