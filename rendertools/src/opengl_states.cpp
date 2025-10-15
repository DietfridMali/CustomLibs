
#include "opengl_states.h"
#include "array.hpp"
#include <glm/glm.hpp>
#include <glm/vector_relational.hpp>
#include "base_renderer.h"

// =================================================================================================

bool OpenGLStates::BindTexture(GLenum typeID, GLuint texture, int tmuIndex) {
	static int currentTMU = -1;
	if (tmuIndex < 0)
		return false;
#if 0
	baseRenderer.ClearGLError();
	GLint tex = 0;
	glGetIntegeri_v(GL_TEXTURE_BINDING_2D, 0, &tex);
	baseRenderer.CheckGLError("BindTexture");
#endif
#if 0
	ActiveTexture(GL_TEXTURE0 + tmuIndex);
	glBindTexture(typeID, texture);
#else
	int typeIndex = (typeID == GL_TEXTURE_2D) ? 0 : 1;
	if (m_bindings[tmuIndex].handles[typeIndex] != texture) {
		//SetTexture(typeID, true);
		m_bindings[tmuIndex].handles[typeIndex] = texture;
		currentTMU = ActiveTexture(GL_TEXTURE0 + tmuIndex);
		glBindTexture(typeID, texture);
		if (GL_TEXTURE0 + tmuIndex != currentTMU)
			ActiveTexture(currentTMU);
	}
	if (m_maxTMU < tmuIndex + 1)
		m_maxTMU = tmuIndex + 1;
#endif
	return true;
}


int OpenGLStates::TextureIsBound(GLenum typeID, GLuint texture) {
	int j = (typeID == GL_TEXTURE_2D) ? 0 : 1;
	for (int i = 0; i < m_maxTMU; ++i)
		if (m_bindings[i].handles[j] == texture)
			return i;
	return -1;
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

// =================================================================================================
