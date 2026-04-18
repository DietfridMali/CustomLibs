
#include "gfxdriverstates.h"
#include "array.hpp"

#include "base_renderer.h"

#define TRACK_TMU_USAGE 1

// =================================================================================================

int TextureSlotInfo::Find(GLuint handle, int tmuIndex) noexcept {
	for (int i = 0; i < m_maxUsedTMU; ++i)
		if (m_bindings[i] == handle) {
			return i;
		}
	return -1;
}


bool TextureSlotInfo::Update(GLuint handle, int tmuIndex) noexcept {
	if ((tmuIndex < 0) or (tmuIndex >= m_bindings.Length()))
		return false;
	if (m_maxUsedTMU < tmuIndex + 1)
		m_maxUsedTMU = tmuIndex + 1;
	m_bindings[tmuIndex] = handle;
	return true;
}


int TextureSlotInfo::Bind(GLuint handle, int tmuIndex) {
	if (handle != 0) {
		int boundTMU = Find(handle, tmuIndex);
		if (boundTMU == tmuIndex)
			return tmuIndex;
		// binding to a different TMU, so unbind from previous TMU if bound
		if (boundTMU >= 0) {
			glActiveTexture(GL_TEXTURE0 + boundTMU);
			glBindTexture(m_type, 0);
			m_bindings[boundTMU] = 0;
		}
	}
	else if (tmuIndex < 0)
		return std::numeric_limits<int>::min();

	if (not Update(handle, tmuIndex))
		return -1;

	glActiveTexture(GL_TEXTURE0 + tmuIndex);
	glBindTexture(m_type, handle);
	return handle ? tmuIndex : -1;
}


bool TextureSlotInfo::Release(GLuint handle, int tmuIndex) {
	int boundTMU = Find(handle, tmuIndex);
	if (boundTMU < 0)
		return false;
	m_bindings[boundTMU] = 0;
	glActiveTexture(GL_TEXTURE0 + boundTMU);
	glBindTexture(m_type, 0);
	return true;
}

// =================================================================================================

TextureSlotInfo* GfxDriverStates::FindInfo(GLenum type) {
	for (TextureSlotInfo& info : m_tmuBindings)
		if (info.GetType() == type)
			return &info;
	return nullptr;
}


int GfxDriverStates::GetBoundTexture(GLenum type, int tmuIndex) {
#if TRACK_TMU_USAGE
	TextureSlotInfo* info = FindInfo(type);
	if (info)
		return info->Query(tmuIndex);
#endif
	return -1;
}


int GfxDriverStates::SetBoundTexture(GLenum type, GLuint handle, int tmuIndex) {
#if TRACK_TMU_USAGE
	TextureSlotInfo* info = FindInfo(type);
	if (info)
		return info->Update(handle, tmuIndex);
#endif
	return -1;
}


int GfxDriverStates::BoundTMU(GLenum type, GLuint handle, int tmuIndex) {
#if TRACK_TMU_USAGE
	TextureSlotInfo* info = FindInfo(type);
	if (info)
		return info->Find(handle, tmuIndex);
#endif
	return -1;
}


int GfxDriverStates::BindTexture(GLenum type, GLuint handle, int tmuIndex) {
#if 0
	baseRenderer.ClearGfxError();
	GLint tex = 0;
	glGetIntegeri_v(GL_TEXTURE_BINDING_2D, 0, &tex);
#endif

#if !TRACK_TMU_USAGE

	ActiveTexture(GL_TEXTURE0 + tmuIndex);
	glBindTexture(typeID, handle);

#else

	TextureSlotInfo* info = FindInfo(type);
	if (handle != 0) {
		if ((info == nullptr) and not (info = m_tmuBindings.Append(TextureSlotInfo(type))))
			return -1;
	}
	else if (info == nullptr)
		return std::numeric_limits<int>::min();
	return info->Bind(handle, tmuIndex);

#endif
	return (handle == 0) ? -1 : tmuIndex;
}


bool GfxDriverStates::ReleaseTexture(GLenum type, GLuint handle, int tmuIndex) {
	TextureSlotInfo* info = FindInfo(type);
	return info ? info->Release(handle, tmuIndex) : false;
}


void GfxDriverStates::DetermineExtensions(void) {
	GLint extCount = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extCount);
	m_extensions.reserve(extCount);
	for (GLint i = 0; i < extCount; ++i) {
		const char* s = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
		if (s)
			m_extensions.emplace(s);
	}
}


void GfxDriverStates::ReleaseBuffers(void) noexcept {
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glActiveTexture(GL_TEXTURE0);
}

// =================================================================================================
