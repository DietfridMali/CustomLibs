#pragma once

#include "std_defines.h"
#include "array.hpp"
#include "string.hpp"
#include "rendertypes.h"

// =================================================================================================
// The API neutral half of a 2D texture array: the slot geometry and the staging the backends upload
// from. GfxTextureArray adds the upload itself and nothing else, the same way Cubemap adds only its
// six face uploads on top of Texture.
//
// SLOTS, NOT LAYERS. The images in here are unrelated to one another - a smoke sprite next to a spark
// sheet next to a corona - and nothing is stacked, ordered or composited. What the array offers is a
// numbered place to put an image so that one binding serves them all, and "slot" is what that is.
//
// WHY AN ARRAY AND NOT AN ATLAS. Both put many images behind one binding, which is what saves the
// batch. An atlas puts them side by side in one image, and that is why every atlas in this library is
// created with nearest filtering and no mip maps (see VariableTextureAtlas::Create) - neighbouring
// cells sit flush against each other, so a bilinear tap at a cell's edge reads the cell next to it and
// a mip level averages cells that have nothing to do with one another. An array slot has its own
// borders and its own mip chain. Anything that is filtered or minified - a sprite sheet scaled across
// half the screen, say - therefore belongs in an array, and only unfiltered, unminified cells belong
// in an atlas.
//
// EVERY SLOT HAS THE SAME SIZE. That is the price of the array, and SetSlot () pays it: an image
// smaller than the slot is scaled up on the way in. The scaling happens once, when the array is
// filled, not per frame - and it lives here rather than three times over in the backends because it is
// plain work on bytes with no API in it.
//
// ONE FLAT BUFFER, not a container per slot: slot after slot, rows within a slot, is exactly the
// memory layout every backend's 3D upload call wants, so the whole array goes up in one call and the
// staging needs no packing step.

class BaseTextureArray {
protected:
	AutoArray<uint8_t>	m_pixels;
	int					m_slotWidth{ 0 };
	int					m_slotHeight{ 0 };
	int					m_slotCount{ 0 };
	int					m_components{ 4 };
	GfxPixelFormat		m_format{ GfxPixelFormat::RGBA8_UNorm };
	int					m_mipCount{ 1 };
	size_t				m_slotBytes{ 0 };
	String				m_arrayName{ "" };

	// Bilinear resample of a tightly packed image into one slot of m_pixels.
	// Deliberately not a box filter: these are sprites that get MAGNIFIED here, and bilinear is what
	// magnification wants. A box filter only pays off when scaling down, which this never does.
	void ScaleIntoSlot(uint8_t* dst, const uint8_t* src, int srcWidth, int srcHeight);

public:
	BaseTextureArray() = default;

	// Not virtual: nothing holds one of these through a base pointer, exactly as with
	// BaseTextureAtlas. Add a vtable here the moment that changes.
	~BaseTextureArray() = default;

	// Opens the array at the given slot size. Every slot starts out zeroed, so a slot that is never
	// filled samples as fully transparent black rather than as garbage.
	bool CreateSlots(String name, int slotWidth, int slotHeight, int slotCount, int components = 4);

	bool CreateCompressedSlots(String name, int slotWidth, int slotHeight, int slotCount, GfxPixelFormat format, int mipCount);

	void DestroySlots(void);

	// Puts one image in a slot, scaling it up to slot size if it is smaller. `data` is expected as
	// tightly packed rows of `components` bytes per pixel, the same component count the array was
	// created with. An image LARGER than the slot is refused rather than scaled down: that means the
	// array was created too small, and quietly losing resolution would hide the mistake.
	bool SetSlot(int slotIndex, const uint8_t* data, int width, int height, int components);

	bool SetCompressedSlot(int slotIndex, const uint8_t* data, size_t dataSize, int width, int height, GfxPixelFormat format, int mipCount);

	static size_t CompressedChainBytes(int width, int height, GfxPixelFormat format, int mipCount) noexcept;

	// The smallest power of two that holds n. Slot sizes should be rounded up with this so that the
	// scale factor between a source image and its slot is itself a power of two: sprite sheets are
	// grids, and a fractional factor walks the cell boundaries off the grid the texture coordinates
	// assume.
	static int NextPowerOfTwo(int n) noexcept;

	inline int SlotCount(void) const noexcept {
		return m_slotCount;
	}

	inline int SlotWidth(void) const noexcept {
		return m_slotWidth;
	}

	inline int SlotHeight(void) const noexcept {
		return m_slotHeight;
	}

	inline int SlotComponents(void) const noexcept {
		return m_components;
	}

	inline int SlotSize(void) const noexcept {
		return IsCompressed() ? int(m_slotBytes) : m_slotWidth * m_slotHeight * m_components;
	}

	inline bool IsCompressed(void) const noexcept {
		return GfxIsBlockCompressed(m_format);
	}

	inline GfxPixelFormat SlotFormat(void) const noexcept {
		return m_format;
	}

	inline int SlotMipCount(void) const noexcept {
		return m_mipCount;
	}

	inline bool HasSlots(void) noexcept {
		return (m_slotCount > 0) and (m_pixels.Length() > 0);
	}

	bool SlotPointers(AutoArray<const uint8_t*>& slotPtrs);

	// The whole stack, slot after slot - what a 3D upload call reads.
	inline uint8_t* SlotData(void) noexcept {
		return m_pixels.DataPtr();
	}

	inline uint8_t* SlotData(int slotIndex) noexcept {
		return m_pixels.DataPtr() + size_t(slotIndex) * size_t(SlotSize());
	}

	// floor(log2(max(w, h))) + 1, or 1 when mip maps are off.
	int MipCount(bool useMipMaps) const noexcept;

	// Builds a mip chain PER SLOT, 2x2 box filtered and edge clamped, and hands out one pointer per
	// slot into `chains` - each pointing at that slot's levels packed tightly, level 0 first. That is
	// the layout the DX12 and Vulkan array uploads read, one subresource per (slot, level).
	bool BuildMipChains(int mipCount, AutoArray<uint8_t>& chains, AutoArray<const uint8_t*>& slotPtrs, eColorEncoding colorEncoding);
};

// =================================================================================================
