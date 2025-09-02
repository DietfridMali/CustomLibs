
#include "opengl_states.h"
#include "array.hpp"
#include <glm/glm.hpp>
#include <glm/vector_relational.hpp>

// =================================================================================================

void OpenGLStates::BindTexture(GLenum typeID, GLenum tmu, GLuint texture) {
	static GLenum currentTMU = GL_TEXTURE0;
	if (tmu == GL_NONE)
		tmu = currentTMU;
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
#endif
}


// =================================================================================================
