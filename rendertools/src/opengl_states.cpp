
#include "opengl_states.h"
#include "array.hpp"

#include "base_renderer.h"

#define TRACK_TMU_USAGE 1

// =================================================================================================

int OpenGLStates::BoundTMU(GLuint handle, int tmuIndex) {
#if TRACK_TMU_USAGE
	if ((tmuIndex >= 0) and )tmuIndex < m_maxUsedTMU) and (m_tmuBindings[tmuIndex] == handle))
		return tmuIndex;
	for (int i = 0; i < m_maxUsedTMU; ++i)
		if (m_tmuBindings[i] == handle)
			return i;
#endif
	return -1;
}


int OpenGLStates::BindTexture(GLenum typeID, GLuint handle, int tmuIndex) {
	if (tmuIndex < 0)
		return false;
#if 0
	baseRenderer.ClearGLError();
	GLint tex = 0;
	glGetIntegeri_v(GL_TEXTURE_BINDING_2D, 0, &tex);
#endif
#if !TRACK_TMU_USAGE
	ActiveTexture(GL_TEXTURE0 + tmuIndex);
	glBindTexture(typeID, handle);
#else
	if (handle != 0) {
		int boundTMU = BoundTMU(handle, tmuIndex);
		if (boundTMU == tmuIndex)
			return tmuIndex;
		// binding to a different TMU, so unbind from previous TMU if bound
		ActiveTexture(GL_TEXTURE0 + boundTMU);
		glBindTexture(typeID, 0);
		m_tmuBindings[boundTMU] = 0;
	}
	else if (tmuIndex < 0)
		return std::numeric_limits<int>::min();
	m_tmuBindings[tmuIndex] = handle;
	ActiveTexture(GL_TEXTURE0 + tmuIndex);
	glBindTexture(typeID, handle);
	if (m_maxUsedTMU < tmuIndex + 1)
		m_maxUsedTMU = tmuIndex + 1;
#endif
	return (handle == 0) ? -1 : tmuIndex;
}


bool OpenGLStates::ReleaseTexture(GLuint handle, int tmuIndex) {
	int boundTMU = BoundTMU(handle, tmuIndex);
	if (boundTMU < 0)
		return false;
	m_tmuBindings[boundTMU] = 0;
	ActiveTexture(GL_TEXTURE0 + boundTMU);
	glBindTexture(typeID, 0);
	return true;
}

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

// =================================================================================================
