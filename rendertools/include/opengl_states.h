#pragma once

#include <optional>

#include "glew.h"
#include <glm/glm.hpp>
#include "array.hpp"

// =================================================================================================

class OpenGLStates {
public:
	template<GLenum stateID>
	inline std::optional<bool> SetState(bool state) {
		static std::optional<bool> current;
#if 0
		if (not current.has_value()) {
			current = (glIsEnabled(stateID) == GL_TRUE); // expensive and not essential
#endif
			std::optional<bool> bool previous = current;
			if (current != state) {
				current = state;
				if (state)
					glEnable(stateID);
				else
					glDisable(stateID);
			}
			return previous;
		}

		inline std::optional<bool> SetDepthTest(bool state) { return SetState<GL_DEPTH_TEST>(state); }

		inline std::optional<bool> SetBlending(bool state) { return SetState<GL_BLEND>(state); }

		inline std::optional<bool> SetFaceCulling(bool state) { return SetState<GL_CULL_FACE>(state); }

		inline std::optional<bool> SetAlphaTest(bool state) { return SetState<GL_ALPHA_TEST>(state); }

		inline std::optional<bool> SetScissorTest(bool state) { return SetState<GL_SCISSOR_TEST>(state); }

		inline std::optional<bool> SetStencilTest(bool state) { return SetState<GL_STENCIL_TEST>(state); }

		inline std::optional<bool> SetPolygonOffsetFill(bool state) { return SetState<GL_POLYGON_OFFSET_FILL>(state); }

		inline std::optional<bool> SetDither(bool state) { return SetState<GL_DITHER>(state); }

		inline std::optional<bool> SetMultiSample(bool state) { return SetState<GL_MULTISAMPLE>(state); }

		inline std::optional<bool> SetTexture2D(bool state) { return SetState<GL_TEXTURE_2D>(state); }

		inline RGBAColor ClearColor(RGBAColor color) {
			static RGBAColor current = ColorData::Invisible;
			RGBAColor previous = current;
			if (current != color) {
				current = color;
				glClearColor(color.R(), color.G(), color.B(), color.A());
			}
			return previous;
		}

		inline ivec4 ColorMask(ivec4 mask) {
			static ivec4 current = ivec4(-1, -1, -1, -1);
			ivec4 previous = current;
			if (glm::any(current != mask)) {        // <-- wichtig: any()
				current = mask;
				glColorMask(GLboolean(mask.x), GLboolean(mask.y), GLboolean(mask.z), GLboolean(mask.w));
			}
			return previous;
		}

		inline int DepthMask(int mask) {
			static int current = -1;
			int previous = current;
			if (current != mask) {        // <-- wichtig: any()
				current = mask;
				glDepthMask(GLboolean(mask));
			}
			return previous;
		}

		inline GLenum DepthFunc(GLenum func) {
			static GLenum current = GL_NONE;
			GLenum previous = current;
			if (current != func) {
				current = func;
				glDepthFunc(current);
			}
		}

		inline void BlendFunc(GLenum sfactor, GLenum dfactor) {
			static GLenum sCurrent = GL_NONE;
			static GLenum dCurrent = GL_NONE;
			if ((sCurrent != sFactor) or (dCurrent != dFactor)) {
				sCurrent = sFactor;
				dCurrent = dFactor;
				glBlendFunc(sFactor, dFactor);
			}
		}

		inline GLenum FrontFace(GLenum face) {
			static GLenum current = GL_NONE;
			GLenum previous = current;
			if (current != face) {
				current = face;
				glFrontFace(face);
			}
			return previous;
		}

		inline GLenum CullFace(GLenum face) {
			static GLenum current = GL_NONE;
			GLenum previous = current;
			if (current != face) {
				current = face;
				glCullFace(face);
			}
			return previous;
		}

		inline GLenum ActiveTexture(GLenum tmu) {
			static GLenum current = GL_NONE;
			GLenum previous = tmu;
			if (current != tmu) {
				current = tmu;
				glActiveTexture(tmu);
			}
			return previous;
		}

		inline GLint BindTexture(GLenum tmu, GLint texture) {
			static ManagedArray<GLint> binds;
			static GLenum currentTMU = GL_TEXTURE0;
			if (binds.Length() == 0) {
				binds.SetAutoFit(true);
				SetDefaultValue(0);
			}
			if (tmu == GL_NONE)
				tmu = currentTMU;
			int i = int (tmu) - int (GL_TEXTURE0);
			if (binds[i] != texture) {
				SetTexture2D(true);
				binds[i] = texture;
				currentTMU = ActiveTexture(tmu);
				glBind(texture);
			}
		}
	};

	// =================================================================================================
