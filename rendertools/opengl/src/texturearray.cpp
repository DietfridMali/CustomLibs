#include "texturearray.h"

// =================================================================================================

// THE HANDLE FIRST, THE SLOTS AFTER IT. Texture::Create () begins with Destroy (), and Destroy () is
// virtual: it lands in GfxTextureArray::Destroy (), which calls DestroySlots (). Creating the slots
// first therefore threw them away again one line later - and Create () still returned true, because
// the handle was there. The array then had m_slotCount 0, every SetSlot () failed on HasSlots (),
// and whoever filled it discarded the whole array.

bool GfxTextureArray::Create(String name, int slotWidth, int slotHeight, int slotCount, bool useMipMaps) {
	if (not Texture::Create())
		return false;
	if (not CreateSlots(name, slotWidth, slotHeight, slotCount)) {
		Texture::Destroy();
		return false;
	}
	m_name = name;
	m_useMipMaps = useMipMaps;
	return true;
}

// -------------------------------------------------------------------------------------------------

void GfxTextureArray::Destroy(void) {
	Texture::Destroy();
	DestroySlots();
}

// -------------------------------------------------------------------------------------------------
// The whole stack in one call: BaseTextureArray stages slot after slot, rows within a slot, which
// is precisely what glTexImage3D reads for a GL_TEXTURE_2D_ARRAY.
//
// SetParams () runs AFTER the upload, not before - for an uncompressed texture it calls
// glGenerateMipmap, and that has nothing to build mips from until the pixels are up.

bool GfxTextureArray::Deploy(int bufferIndex) {
	if (IsDeployed())
		return true;
	if (not HasSlots())
		return false;
	if (not Bind(0, true))
		return false;

	const GLenum format = (m_components == 1) ? GL_RED : (m_components == 3) ? GL_RGB : GL_RGBA;
	const GLenum internalFormat = (m_components == 1) ? GL_R8 : (m_components == 3) ? GL_RGB8 : GL_RGBA8;

	// Rows are tightly packed whatever the width is - the default of 4 would skew every slot whose
	// row length is not a multiple of it.
	GLint packAlignment = 4;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &packAlignment);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GLint(internalFormat), m_slotWidth, m_slotHeight, m_slotCount,
					 0, format, GL_UNSIGNED_BYTE, reinterpret_cast<const void*>(SlotData()));

	glPixelStorei(GL_UNPACK_ALIGNMENT, packAlignment);

	SetParams();
#ifdef _DEBUG
	gfxStates.CheckError();
#endif
	Release();
	m_isDeployed = true;
	return true;
}

// -------------------------------------------------------------------------------------------------

bool GfxTextureArray::UpdateSlot(int slotIndex) {
	if (not IsDeployed() or (slotIndex < 0) or (slotIndex >= m_slotCount))
		return false;
	if (not Bind(0, true))
		return false;

	const GLenum format = (m_components == 1) ? GL_RED : (m_components == 3) ? GL_RGB : GL_RGBA;

	GLint packAlignment = 4;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &packAlignment);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, slotIndex, m_slotWidth, m_slotHeight, 1,
						 format, GL_UNSIGNED_BYTE,
						 reinterpret_cast<const void*>(SlotData() + size_t(slotIndex) * size_t(SlotSize())));

	glPixelStorei(GL_UNPACK_ALIGNMENT, packAlignment);

	// The mip chain of the changed slot is stale now. glGenerateMipmap rebuilds the whole array's,
	// which is more than is needed but is the only thing GL offers short of building the levels here.
	if (m_useMipMaps)
		glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
#ifdef _DEBUG
	gfxStates.CheckError();
#endif
	Release();
	return true;
}

// =================================================================================================
