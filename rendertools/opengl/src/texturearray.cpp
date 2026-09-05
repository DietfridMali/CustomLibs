#include "texturearray.h"

// =================================================================================================

bool GfxTextureArray::Create(String name, int layerWidth, int layerHeight, int layerCount, bool useMipMaps) {
	if (not CreateLayers(name, layerWidth, layerHeight, layerCount))
		return false;
	if (not Texture::Create()) {
		DestroyLayers();
		return false;
	}
	m_name = name;
	m_useMipMaps = useMipMaps;
	return true;
}

// -------------------------------------------------------------------------------------------------

void GfxTextureArray::Destroy(void) {
	Texture::Destroy();
	DestroyLayers();
}

// -------------------------------------------------------------------------------------------------
// The whole stack in one call: BaseTextureArray stages layer after layer, rows within a layer, which
// is precisely what glTexImage3D reads for a GL_TEXTURE_2D_ARRAY.
//
// SetParams () runs AFTER the upload, not before - for an uncompressed texture it calls
// glGenerateMipmap, and that has nothing to build mips from until the pixels are up.

bool GfxTextureArray::Deploy(int bufferIndex) {
	if (IsDeployed())
		return true;
	if (not HasLayers())
		return false;
	if (not Bind(0, true))
		return false;

	const GLenum format = (m_components == 1) ? GL_RED : (m_components == 3) ? GL_RGB : GL_RGBA;
	const GLenum internalFormat = (m_components == 1) ? GL_R8 : (m_components == 3) ? GL_RGB8 : GL_RGBA8;

	// Rows are tightly packed whatever the width is - the default of 4 would skew every layer whose
	// row length is not a multiple of it.
	GLint packAlignment = 4;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &packAlignment);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GLint(internalFormat), m_layerWidth, m_layerHeight, m_layerCount,
					 0, format, GL_UNSIGNED_BYTE, reinterpret_cast<const void*>(LayerData()));

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

bool GfxTextureArray::UpdateLayer(int layerIndex) {
	if (not IsDeployed() or (layerIndex < 0) or (layerIndex >= m_layerCount))
		return false;
	if (not Bind(0, true))
		return false;

	const GLenum format = (m_components == 1) ? GL_RED : (m_components == 3) ? GL_RGB : GL_RGBA;

	GLint packAlignment = 4;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &packAlignment);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layerIndex, m_layerWidth, m_layerHeight, 1,
						 format, GL_UNSIGNED_BYTE,
						 reinterpret_cast<const void*>(LayerData() + size_t(layerIndex) * size_t(LayerSize())));

	glPixelStorei(GL_UNPACK_ALIGNMENT, packAlignment);

	// The mip chain of the changed layer is stale now. glGenerateMipmap rebuilds the whole array's,
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
