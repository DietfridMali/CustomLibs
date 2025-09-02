#pragma once

#include <optional>

#include "glew.h"
#include <glm/glm.hpp>
#include "array.hpp"
#include "singletonbase.hpp"
#include "colordata.h"

// =================================================================================================

class OpenGLStates 
	: public BaseSingleton<OpenGLStates>
{
private:
	struct textureBindings {
		GLuint	handles[2]; // texture2D, cubemap
	};

	ManagedArray<textureBindings> m_bindings;

public:
	OpenGLStates() {
		GLint tmuCount = 0;
		glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &tmuCount);
		m_bindings.Resize(tmuCount);
		m_bindings.Fill({ 0,0 });
	}


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
#if 0 // obsolete
		template <GLenum typeID>
		inline std::optional<bool> SetTexture(bool state) { return SetState<typeID>(state); }

		inline std::optional<bool> SetTexture2D(bool state) { return SetTexture<GL_TEXTURE_2D>(state); }

		inline std::optional<bool> SetTextureCubemap(bool state) { return SetTexture<GL_TEXTURE_CUBE_MAP>(state); }

		inline std::optional<bool> SetTexture(GLenum typeID, bool state) {
			switch (typeID) {
			case GL_TEXTURE_2D:         
				return SetTexture<GL_TEXTURE_2D>(state);
			case GL_TEXTURE_CUBE_MAP:   
				return SetTexture<GL_TEXTURE_CUBE_MAP>(state);
				// weitere Targets nach Bedarf ...
			default:                    
				return std::nullopt; // oder assert/throw
			}
		}
#endif
		RGBAColor ClearColor(RGBAColor color);

		glm::ivec4 ColorMask(glm::ivec4 mask);

		int DepthMask(int mask);

		GLenum DepthFunc(GLenum func);

		std::optional<bool> BlendFunc(GLenum sfactor, GLenum dfactor);

		GLenum FrontFace(GLenum face);

		GLenum CullFace(GLenum face);

		GLenum ActiveTexture(GLenum tmu);

		void BindTexture(GLenum typeID, GLenum tmu, GLuint texture);

		template <GLenum typeID>
		inline void BindTexture(GLenum tmu, GLuint texture) {
			BindTexture(typeID, tmu, texture);
		}
#if 0
		inline void BindTexture(GLenum typeID, GLenum tmu, GLuint texture) {
			switch (typeID) {
			case GL_TEXTURE_2D:
				BindTexture<GL_TEXTURE_2D>(tmu, texture);
				break;
			case GL_TEXTURE_CUBE_MAP:
				BindTexture<GL_TEXTURE_CUBE_MAP>(tmu, texture);
				break;
			default:
				; // oder assert/throw
			}
		}
#endif
		inline void BindTexture2D(GLenum tmu, GLuint texture) {
			BindTexture<GL_TEXTURE_2D>(tmu, texture);
		}

		inline void BindCubemap(GLenum tmu, GLuint texture) {
			BindTexture<GL_TEXTURE_CUBE_MAP>(tmu, texture);
		}
	};

#define openGLStates OpenGLStates::Instance()

	// =================================================================================================
