
#include "opengl_states.h"
#include "array.hpp"
#include <glm/glm.hpp>
#include <glm/vector_relational.hpp>

// =================================================================================================

RGBAColor OpenGLStates::ClearColor(RGBAColor color) {
	static RGBAColor current = ColorData::Invisible;
	RGBAColor previous = current;
	if (current != color) {
		current = color;
		glClearColor(color.R(), color.G(), color.B(), color.A());
	}
	return previous;
}

glm::ivec4 OpenGLStates::ColorMask(glm::ivec4 mask) {
	static glm::ivec4 current = glm::ivec4(-1, -1, -1, -1);
	glm::ivec4 previous = current;
	if (glm::any(glm::notEqual(current, mask))) {        // <-- wichtig: any()
		current = mask;
		glColorMask(GLboolean(mask.x), GLboolean(mask.y), GLboolean(mask.z), GLboolean(mask.w));
	}
	return previous;
}

int OpenGLStates::DepthMask(int mask) {
	static int current = -1;
	int previous = current;
	if (current != mask) {        // <-- wichtig: any()
		current = mask;
		glDepthMask(GLboolean(mask));
	}
	return previous;
}

GLenum OpenGLStates::DepthFunc(GLenum func) {
	static GLenum current = GL_NONE;
	GLenum previous = current;
	if (current != func) {
		current = func;
		glDepthFunc(current);
	}
	return current;
}

std::optional<bool> OpenGLStates::BlendFunc(GLenum sFactor, GLenum dFactor) {
	std::optional<bool> blending = SetBlending(true);
	static GLenum sCurrent = GL_NONE;
	static GLenum dCurrent = GL_NONE;
	if ((sCurrent != sFactor) or (dCurrent != dFactor)) {
		sCurrent = sFactor;
		dCurrent = dFactor;
		glBlendFunc(sFactor, dFactor);
	}
	return blending;
}

GLenum OpenGLStates::OpenGLStates::FrontFace(GLenum face) {
	static GLenum current = GL_NONE;
	GLenum previous = current;
	if (current != face) {
		current = face;
		glFrontFace(face);
	}
	return previous;
}

GLenum OpenGLStates::CullFace(GLenum face) {
	static GLenum current = GL_NONE;
	GLenum previous = current;
	if (current != face) {
		current = face;
		glCullFace(face);
	}
	return previous;
}

GLenum OpenGLStates::ActiveTexture(GLenum tmu) {
	static GLenum current = GL_NONE;
	GLenum previous = tmu;
	if (current != tmu) {
		current = tmu;
		glActiveTexture(tmu);
	}
	return previous;
}


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
