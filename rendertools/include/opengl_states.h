#pragma once

#include <optional>

#include "glew.h"
#include <glm/glm.hpp>
#include "array.hpp"
#include "colordata.h"

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
			std::optional<bool> previous = current;
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

		template <GLenum typeID>
		inline std::optional<bool> SetTexture(bool state) { return SetState<typeID>(state); }

		inline std::optional<bool> SetTexture2D(bool state) { return SetTexture<GL_TEXTURE_2D>(state); }

		inline std::optional<bool> SetTextureCubemap(bool state) { return SetTexture<GL_TEXTURE_CUBE_MAP>(state); }

		RGBAColor ClearColor(RGBAColor color);

		glm::ivec4 ColorMask(glm::ivec4 mask);

		int DepthMask(int mask);

		GLenum DepthFunc(GLenum func);

		void BlendFunc(GLenum sfactor, GLenum dfactor);

		GLenum FrontFace(GLenum face);

		GLenum CullFace(GLenum face);

		GLenum ActiveTexture(GLenum tmu);

		template <GLenum typeID>
		void BindTexture(GLenum tmu, GLint texture) {
			static ManagedArray<GLint> binds;
			static GLenum currentTMU = GL_TEXTURE0;
			if (binds.Length() == 0) {
				binds.SetAutoFit(true);
				binds.SetDefaultValue(0);
			}
			if (tmu == GL_NONE)
				tmu = currentTMU;
			int i = int(tmu) - int(GL_TEXTURE0);
			if (binds[i] != texture) {
				SetTexture<typeID>(true);
				binds[i] = texture;
				currentTMU = ActiveTexture(tmu);
				glBindTexture(GL_TEXTURE_2D, texture);
			}
		}

		inline void BindTexture2D(GLenum tmu, GLint texture) {
			BindTexture<GL_TEXTURE_2D>(tmu, texture);
		}

		inline void BindCubemap(GLenum tmu, GLint texture) {
			BindTexture<GL_TEXTURE_CUBE_MAP>(tmu, texture);
		}
	};

	// =================================================================================================
