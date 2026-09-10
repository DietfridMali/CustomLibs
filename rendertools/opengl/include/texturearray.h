#pragma once

#include "texture.h"
#include "base_texturearray.h"

// =================================================================================================
// A GL_TEXTURE_2D_ARRAY. Everything about slots and staging is in BaseTextureArray; what is added
// here is the upload, exactly as Cubemap adds only its six face uploads on top of Texture.
//
// Not to be confused with TextureArray in texture.h, which is AutoArray<Texture*> - a list of separate
// textures, the opposite of what this is.

class GfxTextureArray
	: public Texture
	, public BaseTextureArray
{
public:
	GfxTextureArray()
		: Texture(0, GL_TEXTURE_2D_ARRAY, GL_CLAMP_TO_EDGE)
	{}

	// The overload below would otherwise hide Texture's parameterless Create ().
	using Texture::Create;

	// Sprite sheets must not wrap: a bilinear tap at u = 1 would read column 0 back in.
	// The mip chain is worth having here - a particle sprite is minified as often as it is magnified -
	// and unlike an atlas an array can have one, because a mip level never mixes two slots.
	bool Create(String name, int slotWidth, int slotHeight, int slotCount, bool useMipMaps = true);

	bool CreateCompressed(String name, int slotWidth, int slotHeight, int slotCount, GfxPixelFormat format, int mipCount);

	using BaseTextureArray::SetSlot;

	bool SetSlot(int slotIndex, TextureBuffer& buffer);

	// Uploads the whole stack in ONE call - BaseTextureArray stages the slots in exactly the layout
	// glTexImage3D reads. bufferIndex is ignored: the array has no m_buffers, its pixels come from
	// SetSlot ().
	virtual bool Deploy(int bufferIndex = 0) override;

	// Sends one slot up again after it changed, without rebuilding the whole array. Only valid once
	// Deploy () has run.
	bool UpdateSlot(int slotIndex);

	virtual void SetParams(bool forceUpdate = false) override;

	virtual void Destroy(void) override;
};

// =================================================================================================
