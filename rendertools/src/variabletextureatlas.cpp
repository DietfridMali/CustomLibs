#include "variabletextureatlas.h"
#include "gfxrenderer.h"

// =================================================================================================

bool VariableTextureAtlas::Create(String name, int width, int height, int layers,
											 GfxPixelFormat format, int scale) {
	if ((width <= 0) or (height <= 0) or (layers <= 0))
		return false;
	Destroy();
	m_atlas = new RenderTarget();
	if (not m_atlas)
		return false;
	m_layers = layers;
	RenderTarget::RTCreationParams params;
	params.name = name;
	params.colorBufferCount = layers;
	params.colorFormat = ToNativeColorFormat(format);
	// Several colour buffers only become simultaneous render targets when the target knows they are
	// meant as MRTs - otherwise all but the first stay unattached (RenderTarget::AttachBuffers ()).
	params.hasMRTs = layers > 1;
	if (not m_atlas->Create(width, height, scale, params)) {
		m_atlas->Destroy();
		delete m_atlas;
		m_atlas = nullptr;
		return false;
	}
	// An atlas is an image store, not a picture: its cells sit flush against each other, so filtering
	// it blurs every cell and bleeds the neighbouring ones in at the edges.
	m_atlas->SetFiltering(GfxFilterMode::Nearest);
	Reset();
	return true;
}


void VariableTextureAtlas::Reset(void) noexcept {
	m_tiles.Clear();
	m_tileCount = 0;
	m_shelfX = 0;
	m_shelfY = 0;
	m_shelfHeight = 0;
}


int VariableTextureAtlas::Add(int width, int height) {
	if (not (m_atlas and (width > 0) and (height > 0)))
		return -1;

	int atlasWidth = m_atlas->GetWidth();
	int atlasHeight = m_atlas->GetHeight();

	if ((width > atlasWidth) or (height > atlasHeight))
		return -1;	// larger than the page itself - no packing can help that
	// Does not fit in what is left of the current shelf: open the next one above it. The new shelf
	// starts at the top edge of the old one, which is its bottom plus the tallest tile it took.
	if (m_shelfX + width > atlasWidth) {
		m_shelfY += m_shelfHeight;
		m_shelfX = 0;
		m_shelfHeight = 0;
	}
	if (m_shelfY + height > atlasHeight)
		return -1;	// page is full

	Tile tile;

	tile.x = m_shelfX;
	tile.y = m_shelfY;
	tile.w = width;
	tile.h = height;
	if (not m_tiles.Append(std::move(tile)))
		return -1;
	m_shelfX += width;
	// The shelf is as tall as its tallest tile - that is what the next shelf has to clear.
	if (m_shelfHeight < height)
		m_shelfHeight = height;
	return m_tileCount++;
}


Vector2f VariableTextureAtlas::TileOffset(int index) noexcept {
	const Tile* tile = GetTile(index);

	if (not (tile and m_atlas))
		return Vector2f::ZERO;

	int atlasWidth = m_atlas->GetWidth();
	int atlasHeight = m_atlas->GetHeight();

	return ((atlasWidth > 0) and (atlasHeight > 0))
		? Vector2f(float(tile->x) / float(atlasWidth), float(tile->y) / float(atlasHeight))
		: Vector2f::ZERO;
}


Vector2f VariableTextureAtlas::TileScale(int index) noexcept {
	const Tile* tile = GetTile(index);

	if (not (tile and m_atlas))
		return Vector2f::ZERO;

	int atlasWidth = m_atlas->GetWidth();
	int atlasHeight = m_atlas->GetHeight();

	return ((atlasWidth > 0) and (atlasHeight > 0))
		? Vector2f(float(tile->w) / float(atlasWidth), float(tile->h) / float(atlasHeight))
		: Vector2f::ZERO;
}


uint32_t VariableTextureAtlas::LayerHandle(int layer) noexcept {
	return (m_atlas and (layer >= 0) and (layer < m_layers)) ? uint32_t(m_atlas->BufferHandle(layer)) : 0;
}


size_t VariableTextureAtlas::LayerSize(void) noexcept {
	return m_atlas ? m_atlas->BufferSize(0) : 0;
}


bool VariableTextureAtlas::Clear(void) {
	if (not m_atlas)
		return false;
	// Layer by layer: activating with a buffer index selects that one colour buffer as the draw
	// target, and the clear follows the draw buffer selection.
	for (int i = 0; i < m_layers; ++i) {
		if (not m_atlas->Activate({ .bufferIndex = i, .drawBufferGroup = RenderTarget::dbSingle, .clear = true }))
			return false;
		m_atlas->Deactivate();
	}
	return true;
}


bool VariableTextureAtlas::WriteLayer(int layer, const void* data, size_t dataSize) {
	return (m_atlas and (layer >= 0) and (layer < m_layers))
		? m_atlas->WriteBuffer(layer, data, dataSize)
		: false;
}


bool VariableTextureAtlas::ReadLayer(int layer, void* buffer, size_t bufferSize) {
	return (m_atlas and (layer >= 0) and (layer < m_layers))
		? m_atlas->ReadBuffer(layer, buffer, bufferSize)
		: false;
}

// =================================================================================================
