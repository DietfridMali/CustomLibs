#pragma once

#include "std_defines.h"
#include "array.hpp"
#include "string.hpp"

// =================================================================================================
// The API neutral half of a 2D texture array: the layer geometry and the staging the backends upload
// from. GfxTextureArray adds the upload itself and nothing else, the same way Cubemap adds only its
// six face uploads on top of Texture.
//
// WHY AN ARRAY AND NOT AN ATLAS. Both put many images behind one binding, which is what saves the
// batch. An atlas puts them side by side in one image, and that is why every atlas in this library is
// created with nearest filtering and no mip maps (see VariableTextureAtlas::Create) - neighbouring
// cells sit flush against each other, so a bilinear tap at a cell's edge reads the cell next to it and
// a mip level averages cells that have nothing to do with one another. An array layer has its own
// borders and its own mip chain. Anything that is filtered or minified - a sprite sheet scaled across
// half the screen, say - therefore belongs in an array, and only unfiltered, unminified cells belong
// in an atlas.
//
// EVERY LAYER HAS THE SAME SIZE. That is the price of the array, and SetLayer () pays it: an image
// smaller than the layer is scaled up on the way in. The scaling happens once, when the array is
// filled, not per frame - and it lives here rather than three times over in the backends because it is
// plain work on bytes with no API in it.
//
// ONE FLAT BUFFER, not a container per layer: layer after layer, rows within a layer, is exactly the
// memory layout every backend's 3D upload call wants, so the whole array goes up in one call and the
// staging needs no packing step.

class BaseTextureArray {
protected:
	AutoArray<uint8_t>	m_pixels;
	int					m_layerWidth{ 0 };
	int					m_layerHeight{ 0 };
	int					m_layerCount{ 0 };
	int					m_components{ 4 };
	String				m_arrayName{ "" };

	// Bilinear resample of a tightly packed image into one layer of m_pixels.
	// Deliberately not a box filter: these are sprites that get MAGNIFIED here, and bilinear is what
	// magnification wants. A box filter only pays off when scaling down, which this never does.
	void ScaleIntoLayer(uint8_t* dst, const uint8_t* src, int srcWidth, int srcHeight);

public:
	BaseTextureArray() = default;

	// Not virtual: nothing holds one of these through a base pointer, exactly as with
	// BaseTextureAtlas. Add a vtable here the moment that changes.
	~BaseTextureArray() = default;

	// Opens the array at the given layer size. Every layer starts out zeroed, so a layer that is never
	// filled samples as fully transparent black rather than as garbage.
	bool CreateLayers(String name, int layerWidth, int layerHeight, int layerCount, int components = 4);

	void DestroyLayers(void);

	// Puts one image on a layer, scaling it up to layer size if it is smaller. `data` is expected as
	// tightly packed rows of `components` bytes per pixel, the same component count the array was
	// created with. An image LARGER than the layer is refused rather than scaled down: that means the
	// array was created too small, and quietly losing resolution would hide the mistake.
	bool SetLayer(int layerIndex, const uint8_t* data, int width, int height, int components);

	// The smallest power of two that holds n. Layer sizes should be rounded up with this so that the
	// scale factor between a source image and its layer is itself a power of two: sprite sheets are
	// grids, and a fractional factor walks the cell boundaries off the grid the texture coordinates
	// assume.
	static int NextPowerOfTwo(int n) noexcept;

	inline int LayerCount(void) const noexcept {
		return m_layerCount;
	}

	inline int LayerWidth(void) const noexcept {
		return m_layerWidth;
	}

	inline int LayerHeight(void) const noexcept {
		return m_layerHeight;
	}

	inline int LayerComponents(void) const noexcept {
		return m_components;
	}

	inline int LayerSize(void) const noexcept {
		return m_layerWidth * m_layerHeight * m_components;
	}

	inline bool HasLayers(void) noexcept {
		return (m_layerCount > 0) and (m_pixels.Length() > 0);
	}

	// The whole stack, layer after layer - what a 3D upload call reads.
	inline uint8_t* LayerData(void) noexcept {
		return m_pixels.DataPtr();
	}

	inline uint8_t* LayerData(int layerIndex) noexcept {
		return m_pixels.DataPtr() + size_t(layerIndex) * size_t(LayerSize());
	}

	// floor(log2(max(w, h))) + 1, or 1 when mip maps are off.
	int MipCount(bool useMipMaps) const noexcept;

	// Builds a mip chain PER LAYER, 2x2 box filtered and edge clamped, and hands out one pointer per
	// layer into `chains` - each pointing at that layer's levels packed tightly, level 0 first. That is
	// the layout the DX12 and Vulkan array uploads read, one subresource per (layer, level).
	//
	// OpenGL does not need this: glGenerateMipmap does the same thing driver side. The split follows
	// the one texture_mips.h already makes for 3D textures - CPU chain for DX/VK, driver for OGL - and
	// the result is functionally the same pyramid either way.
	bool BuildMipChains(int mipCount, AutoArray<uint8_t>& chains, AutoArray<const uint8_t*>& layerPtrs);
};

// =================================================================================================
