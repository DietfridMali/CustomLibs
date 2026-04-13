#pragma once

#include <optional>
#include <tuple>
#include <utility>
#include <unordered_set>

#include "glew.h"
#include "gfxdrivertypes.h"

#include "array.hpp"
#include "list.hpp"
#include "basesingleton.hpp"
#include "colordata.h"

#define ENFORCE_STATE false

// =================================================================================================

class TextureSlotInfo {
private:
	GLenum m_type{ GL_TEXTURE_2D };
	GLint m_tmuCount{ 0 };
	GLint m_maxUsedTMU{ 0 };
	AutoArray<GLuint> m_bindings;

public:
	TextureSlotInfo(GLenum type = 0) {
		glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &m_tmuCount);
		m_bindings.Resize(m_tmuCount);
		m_bindings.Fill(0);
		m_type = type;
		m_maxUsedTMU = 0;
	}

	int Find(GLuint handle, int tmuIndex = -1) noexcept;
	int Bind(GLuint handle, int tmuIndex);
	bool Release(GLuint handle, int tmuIndex);

	inline GLenum GetType(void) const noexcept {
		return m_type;
	}

	bool Update(GLuint handle, int tmuIndex) noexcept;

	inline GLuint Query(int tmuIndex) const noexcept {
		return ((tmuIndex >= 0) and (tmuIndex < m_maxUsedTMU)) ? m_bindings[tmuIndex] : 0;
	}
};

// =================================================================================================

class GfxDriverStates
	: public BaseSingleton<GfxDriverStates>
{
private:
	int m_maxTextureSize{ 0 };
	std::unordered_set<std::string> m_extensions;
	bool m_haveExtensions{ false };

	List<TextureSlotInfo> m_tmuBindings;

public:
	GfxDriverStates() {
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);
		DetermineExtensions();
	}

	void DetermineExtensions(void);

	inline bool HasExtension(const char* extension) {
		return m_extensions.find(extension) != m_extensions.end();
	}

	inline int MaxTextureSize(void) noexcept {
		return m_maxTextureSize;
	}

	template<GLenum stateID>
	inline int SetState(int state) {
		static int current = -1;
		if (state < 0)
			return current;
			int previous = current;
			if (ENFORCE_STATE or (current != state)) {
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

		inline int SetScissorTest(int state) { return SetState<GL_SCISSOR_TEST>(state); }

		inline int SetStencilTest(int state) { return SetState<GL_STENCIL_TEST>(state); }

		inline void StencilFunc(GLenum func, uint8_t ref, uint8_t mask) {
			static int32_t stateID = -1;
			FuncState(stateID, std::make_tuple(func, GLint(ref), GLuint(mask)),
				[](GLenum f, GLint r, GLuint m) { glStencilFunc(f, r, m); });
		}

		inline void StencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
			static int32_t stateID = -1;
			FuncState(stateID, std::make_tuple(sfail, dpfail, dppass), glStencilOp);
		}

		inline void StencilOpBack(GLenum sfail, GLenum dpfail, GLenum dppass) {
			static int32_t stateID = -1;
			FuncState(stateID, std::make_tuple(sfail, dpfail, dppass),
				[](GLenum sf, GLenum dp, GLenum dpp) { glStencilOpSeparate(GL_BACK, sf, dp, dpp); });
		}

		inline int SetPolygonOffsetFill(int state) { return SetState<GL_POLYGON_OFFSET_FILL>(state); }

		inline int SetDither(int state) { return SetState<GL_DITHER>(state); }

		inline int SetMultiSample(int state) { return SetState<GL_MULTISAMPLE>(state); }

		template <class T>
		struct StateRegistry {
			static inline AutoArray<T> list{};
		};

		template <typename STATE_T, STATE_T unknown, class FUNC_T>
		STATE_T FuncState(STATE_T state, int32_t& stateID, FUNC_T&& glFunc) {
			auto& currentList = StateRegistry<STATE_T>::list;

			bool initialized = stateID >= 0;
			if (not initialized) {
				stateID = currentList.Length();
				currentList.Append(state);
			}
			STATE_T& current = currentList[stateID];
			if (state == unknown)
				return current;
			STATE_T previous = current;
			if (ENFORCE_STATE or not initialized or (current != state)) {
				current = state;
				std::forward<FUNC_T>(glFunc) (state);
			}
			return previous;
		}

		template<class F, class... Args>
		auto FuncState(int32_t& stateID, const std::tuple<Args...>& state, F&& glFunc)
			-> std::tuple<Args...>
		{
			bool initialized = stateID >= 0;
			auto& currentList = MultiStateRegistry<Args...>::list;
			if (not initialized) {
				stateID = currentList.Length();
				currentList.Append(state);
			}
			auto& current = currentList[stateID];
			auto previous = current;
			if (ENFORCE_STATE or not initialized or (current != state)) {
				current = state;
				std::apply(std::forward<F>(glFunc), state);
			}
			return previous;
		}

		template <class... Args>
		struct MultiStateRegistry {
			static inline AutoArray<std::tuple<Args...>> list{};
		};

		inline GLenum DepthFunc(GLenum state) {
			static int32_t stateID = -1;
			return FuncState<GLenum, GL_NONE>(state, stateID, glDepthFunc);
		}

		inline GLenum BlendEquation(GLenum state) {
			static int32_t stateID = -1;
			return FuncState<GLenum, GL_NONE>(state, stateID, glBlendEquation);
		}

		inline GLenum FrontFace(GLenum state) {
			static int32_t stateID = -1;
			return FuncState<GLenum, GL_NONE>(state, stateID, glFrontFace);
		}

		inline GLenum CullFace(GLenum state) {
			static int32_t stateID = -1;
			return FuncState<GLenum, GL_NONE>(state, stateID, glCullFace);
		}

		inline GLenum ActiveTexture(GLenum state) {
			static int32_t stateID = -1;
			return FuncState<GLenum, GL_NONE>(state, stateID, glActiveTexture);
		}

		inline int SetDepthWrite(int state) {
			static int32_t stateID = -1;
			return FuncState<GLboolean, GLboolean(-1)>(GLboolean(state), stateID, glDepthMask);
		}

		inline std::tuple<GLboolean, GLboolean, GLboolean, GLboolean> ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
			static int32_t stateID = -1;
			return FuncState(stateID, std::make_tuple(r, g, b, a), glColorMask);
		}

		inline std::tuple<float, float, float, float> ClearColor(float r, float g, float b, float a) {
			static int32_t stateID = -1;
			return FuncState(stateID, std::make_tuple(r, g, b, a), glClearColor);
		}

		inline RGBAColor ClearColor(RGBAColor color) {
			auto t = ClearColor(color.R(), color.G(), color.B(), color.A());
			return RGBAColor{ std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t) };
		}

		inline std::tuple<GLenum, GLenum> BlendFunc(GLenum sFactor, GLenum dFactor) {
			static int32_t stateID = -1;
			return FuncState(stateID, std::make_tuple(sFactor, dFactor), glBlendFunc);
		}

		inline std::tuple<GLenum, GLenum, GLenum, GLenum> BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA) {
			static int32_t stateID = -1;
			return FuncState(stateID, std::make_tuple(srcRGB, dstRGB, srcA, dstA), glBlendFuncSeparate);
		}

		TextureSlotInfo* FindInfo(GLenum type);

		int BoundTMU(GLenum type, GLuint handle, int tmuIndex = -1);
		int BindTexture(GLenum type, GLuint handle, int tmuIndex);
		bool ReleaseTexture(GLenum type, GLuint handle, int tmuIndex = -1);
		int GetBoundTexture(GLenum type, int tmuIndex);
		int SetBoundTexture(GLenum type, GLuint handle, int tmuIndex);

		template <GLenum typeID>
		inline bool BindTexture(GLuint texture, int tmuIndex) {
			return BindTexture(typeID, texture, tmuIndex);
		}

		inline bool BindTexture2D(GLuint texture, int tmuIndex) {
			return BindTexture<GL_TEXTURE_2D>(texture, tmuIndex);
		}

		inline bool BindCubemap(GLuint texture, int tmuIndex) {
			return BindTexture<GL_TEXTURE_CUBE_MAP>(texture, tmuIndex);
		}

		void ReleaseBuffers(void) noexcept;

		inline void GetViewport(GfxDriverTypes::Int* vp) noexcept {
			glGetIntegerv(GL_VIEWPORT, vp);
		}

		inline void SetViewport(const GfxDriverTypes::Int* vp) noexcept {
			glViewport(vp[0], vp[1], vp[2], vp[3]);
		}
	};

#define gfxDriverStates GfxDriverStates::Instance()

// =================================================================================================
