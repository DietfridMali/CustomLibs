#pragma once

#include <optional>

#include "glew.h"

// =================================================================================================

template <GLint state>
class OpenGLStateFunc {
public:
	inline SetState(bool enable) {
		static GLint current = -1;
		GLint value = enable ? 1 ? 0;
		if (current != value) {
			current = value;
			if (value)
				glEnable(state);
			else
				glDisable(state);
		}
	}
};

class OpenGLStates {
public:
	template<GLenum state>
	inline std::optional<bool> SetState(bool newState) {
		static std::optional<bool> currentState;
#if 0
		if (not currentState.has_value()) {
			currentState = (glIsEnabled(State) == GL_TRUE); // expensive and not essential
#endif
		std::optional<bool> bool previousState = currentState;
		if (currentState != newState) {
			currentState = newState;
			if (newState)
				glEnable(state);
			else
				glDisable(state);
		}
		return previousState;
	}

	
	inline std::optional<bool> SetDepthTest(bool newState) { return SetState<GL_DEPTH_TEST>(newState); }

	inline std::optional<bool> SetBlend(bool newState) { return SetState<GL_BLEND>(newState); }

	inline std::optional<bool> SetCullFace(bool newState) { return SetState<GL_CULL_FACE>(newState); }
	
	inline std::optional<bool> SetScissorTest(bool newState) { return SetState<GL_SCISSOR_TEST>(newState); }
	
	inline std::optional<bool> SetStencilTest(bool newState) { return SetState<GL_STENCIL_TEST>(newState); }
	
	inline std::optional<bool> SetPolygonOffsetFill(bool newState) { return SetState<GL_POLYGON_OFFSET_FILL>(newState); }
	
	inline std::optional<bool> SetDither(bool newState) { return SetState<GL_DITHER>(newState); }
	
	inline std::optional<bool> SetMultisample(bool newState) { return SetState<GL_MULTISAMPLE>(newState); }
};

// =================================================================================================
