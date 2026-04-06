
#include "opengl_states.h"
#include "array.hpp"

#include "base_renderer.h"

#define TRACK_TMU_USAGE 1

// =================================================================================================

void OpenGLStates::DetermineExtensions(void) {
	GLint extCount = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &extCount);
	m_extensions.reserve(extCount);
	for (GLint i = 0; i < extCount; ++i) {
		const char* s = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
		if (s)
			m_extensions.emplace(s);
	}
}


void OpenGLStates::ReleaseBuffers(void) noexcept {
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glActiveTexture(GL_TEXTURE0);
}


int TMUBindingInfo::Find(GLuint handle, int tmuIndex) {
	for (int i = 0; i < m_maxUsedTMU; ++i)
		if (m_bindings[i] == handle) {
			return i;
		}
	return -1;
}


void TMUBindingInfo::Update(GLuint handle, int tmuIndex) {
	m_bindings[tmuIndex] = handle;
}


int TMUBindingInfo::Bind(GLuint handle, int tmuIndex) {
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

	m_bindings[tmuIndex] = handle;
	glActiveTexture(GL_TEXTURE0 + tmuIndex);
	glBindTexture(m_type, handle);
	if (m_maxUsedTMU < tmuIndex + 1)
		m_maxUsedTMU = tmuIndex + 1;
	return handle ? tmuIndex : -1;
}


bool TMUBindingInfo::Release(GLuint handle, int tmuIndex) {
	int boundTMU = Find(handle, tmuIndex);
	if (boundTMU < 0)
		return false;
	m_bindings[boundTMU] = 0;
	glActiveTexture(GL_TEXTURE0 + boundTMU);
	glBindTexture(m_type, 0);
	return true;
}

// =================================================================================================

TMUBindingInfo* OpenGLStates::FindInfo(GLenum type) {
	for (TMUBindingInfo& info : m_tmuBindings)
		if (info.GetType() == type)
			return &info;
	return nullptr;
}


int OpenGLStates::BoundTMU(GLenum type, GLuint handle, int tmuIndex) {
#if TRACK_TMU_USAGE
	TMUBindingInfo* info = FindInfo(type);
	if (info)
		return info->Find(handle, tmuIndex);
#endif
	return -1;
}


int OpenGLStates::BindTexture(GLenum type, GLuint handle, int tmuIndex) {
#if 0
	baseRenderer.ClearGLError();
	GLint tex = 0;
	glGetIntegeri_v(GL_TEXTURE_BINDING_2D, 0, &tex);
#endif

#if !TRACK_TMU_USAGE

	ActiveTexture(GL_TEXTURE0 + tmuIndex);
	glBindTexture(typeID, handle);

#else

	TMUBindingInfo* info = FindInfo(type);
	if (handle != 0) {
		if ((info == nullptr) and not (info = m_tmuBindings.Append(TMUBindingInfo(type))))
			return -1;
	}
	else if (info == nullptr)
		return std::numeric_limits<int>::min();
	return info->Bind(handle, tmuIndex);

#endif
	return (handle == 0) ? -1 : tmuIndex;
}


bool OpenGLStates::ReleaseTexture(GLenum type, GLuint handle, int tmuIndex) {
	TMUBindingInfo* info = FindInfo(type);
	return info ? info->Release(handle, tmuIndex) : false;
}

// =================================================================================================
