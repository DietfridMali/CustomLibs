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
	if (m_atlas)
		m_packer.Reset(m_atlas->GetWidth(), m_atlas->GetHeight());
}


int VariableTextureAtlas::Add(int width, int height) {
	if (not m_atlas)
		return -1;

	SkylinePacker::Place place;

	// A refusal leaves the packer untouched, so the same tile can be offered to the next atlas.
	if (not m_packer.Add(width, height, place))
		return -1;

	Tile tile;

	tile.x = place.x;
	tile.y = place.y;
	tile.w = width;
	tile.h = height;
	if (not m_tiles.Append(std::move(tile)))
		return -1;
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


Texture* VariableTextureAtlas::LayerTexture(int layer) noexcept {
	return (m_atlas and (layer >= 0) and (layer < m_layers)) ? m_atlas->GetAsTexture({ .source = layer }) : nullptr;
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
