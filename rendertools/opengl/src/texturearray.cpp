#include "texturearray.h"
#include "gfxpixelformat_gl.h"

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

bool GfxTextureArray::CreateCompressed(String name, int slotWidth, int slotHeight, int slotCount, GfxPixelFormat format, int mipCount) {
	if (not Texture::Create())
		return false;
	if (not CreateCompressedSlots(name, slotWidth, slotHeight, slotCount, format, mipCount)) {
		Texture::Destroy();
		return false;
	}
	m_name = name;
	m_useMipMaps = (mipCount > 1);
	m_compression = GfxFormatToCompression(format);
	return true;
}

// -------------------------------------------------------------------------------------------------

bool GfxTextureArray::SetSlot(int slotIndex, TextureBuffer& buffer) {
	if (IsCompressed())
		return SetCompressedSlot(slotIndex, buffer.DataBuffer(), size_t(buffer.m_info.m_dataSize),
								 buffer.m_info.m_width, buffer.m_info.m_height, buffer.m_info.m_gfxFormat, buffer.m_info.m_mipCount);
	return SetSlot(slotIndex, buffer.DataBuffer(), buffer.m_info.m_width, buffer.m_info.m_height, buffer.m_info.m_componentCount);
}

// -------------------------------------------------------------------------------------------------

void GfxTextureArray::Destroy(void) {
	Texture::Destroy();
	DestroySlots();
}

// -------------------------------------------------------------------------------------------------

void GfxTextureArray::SetParams(bool forceUpdate) {
	if (not IsCompressed()) {
		Texture::SetParams(forceUpdate);
		return;
	}
	if (forceUpdate or not m_hasParams) {
		m_hasParams = true;
		if (m_mipCount > 1) {
			glTexParameteri(m_type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(m_type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(m_type, GL_TEXTURE_MAX_LEVEL, m_mipCount - 1);
			GLfloat maxAniso = 1.0f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
			glTexParameterf(m_type, GL_TEXTURE_MAX_ANISOTROPY_EXT, (maxAniso < 16.0f) ? maxAniso : 16.0f);
		}
		else {
			glTexParameteri(m_type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(m_type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(m_type, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(m_type, GL_TEXTURE_MAX_LEVEL, 0);
			glTexParameterf(m_type, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
		}
		glTexParameteri(m_type, GL_TEXTURE_WRAP_S, m_wrapMode);
		glTexParameteri(m_type, GL_TEXTURE_WRAP_T, m_wrapModeV);
	}
}

// -------------------------------------------------------------------------------------------------

static void UploadCompressedSlot(GLenum internalFormat, int slotIndex, int slotWidth, int slotHeight, int mipCount, const uint8_t* chain, uint32_t blockBytes) {
	int w = slotWidth;
	int h = slotHeight;
	for (int mip = 0; mip < mipCount; mip++) {
		const GLsizei imgSize = GLsizei(uint32_t((w + 3) / 4) * uint32_t((h + 3) / 4) * blockBytes);
		glCompressedTexSubImage3D(GL_TEXTURE_2D_ARRAY, mip, 0, 0, slotIndex, w, h, 1, internalFormat, imgSize, chain);
		chain += imgSize;
		w = (w > 1) ? (w >> 1) : 1;
		h = (h > 1) ? (h >> 1) : 1;
	}
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

	if (IsCompressed()) {
		const GLenum   internalFormat = ToGLFormat(GfxLinearFormat(m_format)).internalFormat;
		const uint32_t blockBytes = GfxBlockBytes(m_format);
		glTexStorage3D(GL_TEXTURE_2D_ARRAY, m_mipCount, internalFormat, m_slotWidth, m_slotHeight, m_slotCount);
		for (int slot = 0; slot < m_slotCount; slot++)
			UploadCompressedSlot(internalFormat, slot, m_slotWidth, m_slotHeight, m_mipCount, SlotData(slot), blockBytes);
		SetParams();
#ifdef _DEBUG
		gfxStates.CheckError();
#endif
		Release();
		m_isDeployed = true;
		return true;
	}

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

	if (IsCompressed()) {
		UploadCompressedSlot(ToGLFormat(GfxLinearFormat(m_format)).internalFormat, slotIndex, m_slotWidth, m_slotHeight,
							 m_mipCount, SlotData(slotIndex), GfxBlockBytes(m_format));
#ifdef _DEBUG
		gfxStates.CheckError();
#endif
		Release();
		return true;
	}

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
