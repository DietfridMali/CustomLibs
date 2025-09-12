
#include "opengl_states.h"
#include "array.hpp"
#include <glm/glm.hpp>
#include <glm/vector_relational.hpp>
#include "base_renderer.h"

// =================================================================================================

void OpenGLStates::BindTexture(GLenum typeID, GLuint texture, GLenum tmu) {
	static GLenum currentTMU = GL_TEXTURE0;
	if (tmu == GL_NONE)
		tmu = GL_TEXTURE0;
#if 1
	baseRenderer.ClearGLError();
	GLint tex = 0;
	glGetIntegeri_v(GL_TEXTURE_BINDING_2D, 0, &tex);
	baseRenderer.CheckGLError("BindTexture");
#endif
#if 0
	ActiveTexture(tmu);
	glBindTexture(typeID, texture);
#else
	int i = int(tmu) - int(GL_TEXTURE0);
	int j = (typeID == GL_TEXTURE_2D) ? 0 : 1;
	if (m_bindings[i].handles[j] != texture) {
		//SetTexture(typeID, true);
		m_bindings[i].handles[j] = texture;
		currentTMU = ActiveTexture(tmu);
		glBindTexture(typeID, texture);
	}
	if (m_maxTMU < i + 1)
		m_maxTMU = i + 1;
#endif
}


GLenum OpenGLStates::TextureIsBound(GLenum typeID, GLuint texture) {
	int j = (typeID == GL_TEXTURE_2D) ? 0 : 1;
	for (int i = 0; j < m_maxTMU; ++i)
		if (m_bindings[i].handles[j] == texture)
			return i;
	return GL_NONE;
}

// =================================================================================================
