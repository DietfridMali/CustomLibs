
#include "opengl_states.h"

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

ivec4 OpenGLStates::ColorMask(ivec4 mask) {
	static ivec4 current = ivec4(-1, -1, -1, -1);
	ivec4 previous = current;
	if (glm::any(current != mask)) {        // <-- wichtig: any()
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
}

void OpenGLStates::BlendFunc(GLenum sfactor, GLenum dfactor) {
	static GLenum sCurrent = GL_NONE;
	static GLenum dCurrent = GL_NONE;
	if ((sCurrent != sFactor) or (dCurrent != dFactor)) {
		sCurrent = sFactor;
		dCurrent = dFactor;
		glBlendFunc(sFactor, dFactor);
	}
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

GLint OpenGLStates::BindTexture(GLenum tmu, GLint texture) {
	static ManagedArray<GLint> binds;
	static GLenum currentTMU = GL_TEXTURE0;
	if (binds.Length() == 0) {
		binds.SetAutoFit(true);
		SetDefaultValue(0);
	}
	if (tmu == GL_NONE)
		tmu = currentTMU;
	int i = int(tmu) - int(GL_TEXTURE0);
	if (binds[i] != texture) {
		SetTexture2D(true);
		binds[i] = texture;
		currentTMU = ActiveTexture(tmu);
		glBind(texture);
	}
}

// =================================================================================================
