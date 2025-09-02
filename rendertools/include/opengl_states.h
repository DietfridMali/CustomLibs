#pragma once

#include <optional>
#include <tuple>
#include <utility>

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
	inline int SetState(int state) {
		static int current = -1;
		if (state < 0)
			return current;
#if 0
		if (not current.has_value()) {
			current = (glIsEnabled(stateID) == GL_TRUE); // expensive and not essential
#endif
			int previous = current;
			if (current != state) {
				current = state;
				if (state)
					glEnable(stateID);
				else
					glDisable(stateID);
			}
			return previous;
		}

		inline int SetDepthTest(int state) { return SetState<GL_DEPTH_TEST>(state); }

		inline int SetBlending(int state) { return SetState<GL_BLEND>(state); }

		inline int SetFaceCulling(int state) { return SetState<GL_CULL_FACE>(state); }

		inline int SetAlphaTest(int state) { return SetState<GL_ALPHA_TEST>(state); }

		inline int SetScissorTest(int state) { return SetState<GL_SCISSOR_TEST>(state); }

		inline int SetStencilTest(int state) { return SetState<GL_STENCIL_TEST>(state); }

		inline int SetPolygonOffsetFill(int state) { return SetState<GL_POLYGON_OFFSET_FILL>(state); }

		inline int SetDither(int state) { return SetState<GL_DITHER>(state); }

		inline int SetMultiSample(int state) { return SetState<GL_MULTISAMPLE>(state); }
#if 0 // obsolete
		template <GLenum typeID>
		inline int SetTexture(int state) { return SetState<typeID>(state); }

		inline int SetTexture2D(int state) { return SetTexture<GL_TEXTURE_2D>(state); }

		inline int SetTextureCubemap(int state) { return SetTexture<GL_TEXTURE_CUBE_MAP>(state); }

		inline int SetTexture(GLenum typeID, int state) {
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
		template <typename STATE_T, STATE_T None, class FUNC_T>
		STATE_T FuncState(STATE_T state, FUNC_T&& glFunc) {
			static STATE_T current = None;
			if (state == None) 
				return current; 
			STATE_T previous = current;
			if (current != state) { 
				current = state; 
				std::forward<FUNC_T>(glFunc) (state);
			} 
			return previous; 
		}

		template<class F, class... Args>
		auto FuncState(const std::tuple<Args...>& state, F&& glFunc)
			-> std::tuple<Args...>
		{
			static bool initialized = false;
			static std::tuple<Args...> current;

			auto previous = current;
			if (not initialized or (current != state)) {
				initialized = true;
				current = state;
				std::apply(std::forward<F>(glFunc), state); // ruft GL-Funktion mit Args...
			}
			return previous;
		}

		inline GLenum DepthFunc(GLenum state) {
			return FuncState<GLenum, GL_NONE>(state, glDepthFunc);
		}

		inline GLenum BlendEquation(GLenum state) {
			return FuncState<GLenum, GL_NONE>(state, glBlendEquation);
		}

		inline GLenum FrontFace(GLenum state) {
			return FuncState<GLenum, GL_NONE>(state, glFrontFace);
		}

		inline GLenum CullFace(GLenum state) {
			return FuncState<GLenum, GL_NONE>(state, glCullFace);
		}

		inline GLenum ActiveTexture(GLenum state) {
			return FuncState<GLenum, GL_NONE>(state, glActiveTexture);
		}

		inline int DepthMask(int state) {
			return FuncState<int, -1>(state, glDepthMask);
		}

		inline std::tuple<int, int, int, int>ColorMask(int r, int g, int b, int a) {
			return FuncState(std::make_tuple(r, g, b, a), glColorMask);
		}

		inline std::tuple<float, float, float, float>ClearColor(float r, float g, float b, float a) {
			return FuncState(std::make_tuple(r, g, b, a), glClearColor);
		}

		inline RGBAColor ClearColor(RGBAColor color) {
			auto t = ClearColor(color.R(), color.G(), color.B(), color.A());
			return RGBAColor{ std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t) };
		}

		inline std::tuple<GLenum, GLenum> BlendFunc(GLenum sFactor, GLenum dFactor) {
			return FuncState(std::make_tuple(sFactor, dFactor), glBlendFunc);
		}

		inline std::tuple<GLenum, GLenum, GLenum, GLenum> BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA) {
			return FuncState(std::make_tuple(srcRGB, dstRGB, srcA, dstA), glBlendFuncSeparate);
		}

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
