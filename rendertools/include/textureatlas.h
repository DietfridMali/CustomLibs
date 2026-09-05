#pragma once

#include "vector.hpp"
#include "tablesize.h"
#include "texture.h"
#include "rendertarget.h"
#include "colordata.h"
#include "base_quadmesh.h"	// static BaseQuadMesh renderQuad below - the header has to stand alone

// =================================================================================================

// The part every atlas has, whatever its cells look like: one render target holding the image store,
// and the handful of operations that go with it. TextureAtlas puts a fixed grid of glyphs on top of
// this, VariableTextureAtlas a shelf packer with tiles of differing sizes - neither of the two has any
// business knowing about the other's layout, which is why the shared half lives here.

class BaseTextureAtlas {
protected:
	RenderTarget*	m_atlas;
	// An atlas normally owns its target. It may instead be ONE ARRAY LAYER of a target somebody else
	// owns - which is what puts many pages of the same atlas into a single texture, so a shader reaches
	// them through a layer index instead of through a texture binding. m_layer is that page's layer;
	// Activate () selects it, and the owner is responsible for the target's life.
	bool			m_ownsAtlas;
	int				m_layer;

	static BaseQuadMesh	renderQuad;

public:
	BaseTextureAtlas()
		: m_atlas(nullptr), m_ownsAtlas(true), m_layer(0)
	{
	}

	// Not virtual by intent: nothing owns an atlas through a base pointer, and a vtable would be the
	// only thing this class costs. Add one here the moment that changes.
	~BaseTextureAtlas() = default;

	void Destroy(void) {
		if (m_ownsAtlas)
			delete m_atlas;
		m_atlas = nullptr;
		m_ownsAtlas = true;
		m_layer = 0;
	}

	// Sets up the shared quad the render calls below draw with. Once per program run, not per atlas.
	static void Initialize(void);

	bool Render(Shader* shader);

	Texture* GetAsTexture(void) noexcept {
		return m_atlas ? m_atlas->GetAsTexture({}) :  nullptr;
	}

	// Selecting the layer BEFORE activating: Activate () attaches and clears, and a clear has to land on
	// this page's layer rather than on whichever one was selected last.
	inline bool Activate(void) {
		if (m_atlas == nullptr)
			return false;
		if (m_atlas->HasArrayBuffers())
			m_atlas->SelectArrayLayer(m_layer);
		return m_atlas->Activate({ .clear = true });
	}

	inline void Deactivate(void) {
		if (m_atlas)
			m_atlas->Deactivate();
	}

	inline void SetViewport(void) {
		if (m_atlas)
			m_atlas->SetViewport();
	}

	inline int GetWidth(bool scaled = false) noexcept {
		return m_atlas ? m_atlas->GetWidth(scaled) : 0;
	}

	inline int GetHeight(bool scaled = false) noexcept {
		return m_atlas ? m_atlas->GetHeight(scaled) : 0;
	}

	inline RenderTarget* GetRenderTarget(void) noexcept {
		return m_atlas;
	}

	inline bool IsAvailable(void) noexcept {
		return m_atlas != nullptr;
	}

	inline int Layer(void) noexcept {
		return m_layer;
	}

	inline bool OwnsRenderTarget(void) noexcept {
		return m_ownsAtlas;
	}
};

// -------------------------------------------------------------------------------------------------

class TextureAtlas
	: public BaseTextureAtlas
{
public:
	struct GlyphSize {
		int width{ 0 };
		int height{ 0 };
		float aspectRatio{ 1.0f };

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
	TableSize		m_size;
	GlyphSize		m_glyphSize;
	Vector2f		m_scale;

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

	bool RenderColored(int glyphIndex, RGBAColor color = ColorData::White);

	bool RenderGrayscale(int glyphIndex, float brightness = 1.0f);

	bool Add(Texture* glyph, int glyphIndex, Vector2f& scale);

	inline TableSize& Size(void) {
		return m_size;
	}
};

// =================================================================================================
