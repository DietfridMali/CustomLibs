#pragma once

#include "vector.hpp"
#include "list.hpp"
#include "textureatlas.h"
#include "rendertypes.h"

// =================================================================================================
// An atlas whose cells differ in size, packed by a shelf algorithm.
//
// TextureAtlas puts a fixed grid over its render target: every cell has the same size and a cell's
// place follows from its index. That is right for glyphs and wrong for anything measured in world
// units - a light map tile is as large as the surface it lights, and those differ by orders of
// magnitude. This class shares the render target half (BaseTextureAtlas) and replaces the layout.
//
// SHELF PACKING keeps a current row ("shelf") and fills it left to right; when the next tile no
// longer fits in the row's remaining width, a new shelf opens above the tallest tile of the old one.
// It wastes the difference between a shelf's height and the height of its shorter tiles, which is
// why callers should add tiles TALLEST FIRST - then every shelf is filled by tiles of nearly its own
// height and the waste is small. The packer does not sort by itself: it hands out places in the
// order it is asked, so that a caller can keep its own tile order (a face index, say) as the index
// it gets back.
//
// LAYERS are colour buffers of one render target, all of the same size and format. A light map needs
// two (diffuse and ambient) that are written by the same pass and sampled by the same fragment, and
// keeping them in one target means one packing and one set of tile coordinates for both.
//
// One atlas is one page. When Add () refuses because nothing fits any more, the caller opens the
// next atlas - the packer has no notion of a successor, deliberately: whoever owns the pages knows
// how many there may be and what they cost.

class VariableTextureAtlas
	: public BaseTextureAtlas
{
public:
	// Place of one tile, in texels of the unscaled atlas.
	struct Tile {
		int	x{ 0 };
		int	y{ 0 };
		int	w{ 0 };
		int	h{ 0 };

		inline bool IsValid(void) const noexcept {
			return (w > 0) and (h > 0);
		}
	};

protected:
	List<Tile>			m_tiles;
	int					m_tileCount{ 0 };
	int					m_layers{ 1 };
	// The current shelf: its bottom edge, the next free column in it, and the height it has grown to.
	int					m_shelfX{ 0 };
	int					m_shelfY{ 0 };
	int					m_shelfHeight{ 0 };

public:
	VariableTextureAtlas() = default;

	~VariableTextureAtlas() = default;

	// layers = colour buffers, all of size width x height and of the given format. Nearest filtering
	// and no mip maps, like every atlas: the cells sit flush against each other, so filtering bleeds
	// a neighbour in at the edges and a mip level mixes cells that have nothing to do with each other.
	bool Create(String name, int width, int height, int layers = 1,
					GfxPixelFormat format = GfxPixelFormat::RGBA8_UNorm, int scale = 1);

	// Reserves width x height texels and returns the tile's INDEX, or -1 when the atlas is full.
	// Tiles are handed out in call order, so the index is the caller's to use as it likes.
	int Add(int width, int height);

	// Frees every tile without touching the render target, so the same atlas can be packed again.
	void Reset(void) noexcept;

	inline int TileCount(void) noexcept {
		return m_tileCount;
	}

	inline int LayerCount(void) noexcept {
		return m_layers;
	}

	inline const Tile* GetTile(int index) noexcept {
		return ((index >= 0) and (index < m_tileCount)) ? &m_tiles[index] : nullptr;
	}

	// Where the tile sits in the atlas, as texture coordinates in [0,1]. Offset is its lower left
	// corner, scale its size - together they map a unit square onto the tile.
	Vector2f TileOffset(int index) noexcept;

	Vector2f TileScale(int index) noexcept;

	// One layer as a sampleable texture. Layer 0 is what GetAsTexture () hands out.
	uint32_t LayerHandle(int layer) noexcept;

	// One layer's texels into a CPU buffer. bufferSize is the size of that buffer in BYTES and is
	// checked against what the layer actually holds, so a buffer that is too small is refused rather
	// than overrun. See RenderTarget::ReadBuffer () for what this costs.
	bool ReadLayer(int layer, void* buffer, size_t bufferSize);

	// Every layer to the render target's clear colour. A freshly created target holds whatever was in
	// that memory, and an atlas that is never fully written (a level with no lights, the padding
	// between tiles) would otherwise show it.
	bool Clear(void);

	// The other direction: CPU texels into one layer, in the same format ReadLayer () hands out. This
	// is what puts a saved atlas back where it came from.
	bool WriteLayer(int layer, const void* data, size_t dataSize);

	// How many bytes ReadLayer () needs for one layer, and WriteLayer () expects.
	size_t LayerSize(void) noexcept;
};

// =================================================================================================
