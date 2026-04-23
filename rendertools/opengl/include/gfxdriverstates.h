#pragma once

#include <optional>
#include <tuple>
#include <utility>
#include <unordered_set>

#include "glew.h"
#include "gfxdrivertypes.h"
#include "rendertypes.h"

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
// GfxOperations → GLenum conversion (OpenGL backend)

namespace GfxToGL {
	using namespace GfxOperations;

	inline GLenum ToGLenum(CompareFunc f) noexcept {
		switch (f) {
			case CompareFunc::Never:
				return GL_NEVER;
			case CompareFunc::Less:
				return GL_LESS;
			case CompareFunc::Equal:
				return GL_EQUAL;
			case CompareFunc::LessEqual:
				return GL_LEQUAL;
			case CompareFunc::Greater:
				return GL_GREATER;
			case CompareFunc::NotEqual:
				return GL_NOTEQUAL;
			case CompareFunc::GreaterEqual:
				return GL_GEQUAL;
			case CompareFunc::Always:
				return GL_ALWAYS;
			default:
				return GL_LEQUAL;
		}
	}

	inline GLenum ToGLenum(BlendFactor f) noexcept {
		switch (f) {
			case BlendFactor::Zero:
				return GL_ZERO;
			case BlendFactor::One:
				return GL_ONE;
			case BlendFactor::SrcColor:
				return GL_SRC_COLOR;
			case BlendFactor::InvSrcColor:
				return GL_ONE_MINUS_SRC_COLOR;
			case BlendFactor::SrcAlpha:
				return GL_SRC_ALPHA;
			case BlendFactor::InvSrcAlpha:
				return GL_ONE_MINUS_SRC_ALPHA;
			case BlendFactor::DstAlpha:
				return GL_DST_ALPHA;
			case BlendFactor::InvDstAlpha:
				return GL_ONE_MINUS_DST_ALPHA;
			case BlendFactor::DstColor:
				return GL_DST_COLOR;
			case BlendFactor::InvDstColor:
				return GL_ONE_MINUS_DST_COLOR;
			default:
				return GL_ONE;
		}
	}

	inline GLenum ToGLenum(BlendOp op) noexcept {
		switch (op) {
			case BlendOp::Add:
				return GL_FUNC_ADD;
			case BlendOp::Subtract:
				return GL_FUNC_SUBTRACT;
			case BlendOp::RevSubtract:
				return GL_FUNC_REVERSE_SUBTRACT;
			case BlendOp::Min:
				return GL_MIN;
			case BlendOp::Max:
				return GL_MAX;
			default:
				return GL_FUNC_ADD;
		}
	}

	inline GLenum ToGLenum(FaceCull c) noexcept {
		switch (c) {
			case FaceCull::Front:
				return GL_FRONT;
			case FaceCull::Back:
				return GL_BACK;
			case FaceCull::None:
				return GL_FRONT_AND_BACK;
			default:
				return GL_BACK;
		}
	}

	inline GLenum ToGLenum(Winding w) noexcept {
		return (w == Winding::CCW) ? GL_CCW : GL_CW;
	}

	inline GLenum ToGLenum(StencilOp op) noexcept {
		switch (op) {
			case StencilOp::Keep:
				return GL_KEEP;
			case StencilOp::Zero:
				return GL_ZERO;
			case StencilOp::Replace:
				return GL_REPLACE;
			case StencilOp::IncrSat:
				return GL_INCR;
			case StencilOp::DecrSat:
				return GL_DECR;
			case StencilOp::Incr:
				return GL_INCR_WRAP;
			case StencilOp::Decr:
				return GL_DECR_WRAP;
			default:
				return GL_KEEP;
		}
	}
}

// =================================================================================================

class GfxStates
	: public BaseSingleton<GfxStates>
{
private:
	int m_maxTextureSize{ 0 };
	std::unordered_set<std::string> m_extensions;
	bool m_haveExtensions{ false };

	List<TextureSlotInfo> m_tmuBindings;

public:
	GfxStates() {
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

		// GfxOperations overloads — shared code uses these; internally convert to GLenum
		inline GfxOperations::CompareFunc DepthFunc(GfxOperations::CompareFunc func) {
			DepthFunc(GfxToGL::ToGLenum(func));
			return func;
		}

		inline GfxOperations::FaceCull CullFace(GfxOperations::FaceCull mode) {
			CullFace(GfxToGL::ToGLenum(mode));
			return mode;
		}

		inline GfxOperations::Winding FrontFace(GfxOperations::Winding winding) {
			FrontFace(GfxToGL::ToGLenum(winding));
			return winding;
		}

		inline GfxOperations::BlendOp BlendEquation(GfxOperations::BlendOp op) {
			BlendEquation(GfxToGL::ToGLenum(op));
			return op;
		}

		inline void BlendFunc(GfxOperations::BlendFactor src, GfxOperations::BlendFactor dst) {
			BlendFunc(GfxToGL::ToGLenum(src), GfxToGL::ToGLenum(dst));
		}

		inline void BlendFuncSeparate(GfxOperations::BlendFactor srcRGB, GfxOperations::BlendFactor dstRGB,
		                              GfxOperations::BlendFactor srcAlpha, GfxOperations::BlendFactor dstAlpha) {
			BlendFuncSeparate(GfxToGL::ToGLenum(srcRGB), GfxToGL::ToGLenum(dstRGB),
			                  GfxToGL::ToGLenum(srcAlpha), GfxToGL::ToGLenum(dstAlpha));
		}

		inline void StencilFunc(GfxOperations::CompareFunc func, uint8_t ref, uint8_t mask) {
			StencilFunc(GfxToGL::ToGLenum(func), ref, mask);
		}

		inline void StencilOp(GfxOperations::StencilOp sfail, GfxOperations::StencilOp dpfail, GfxOperations::StencilOp dppass) {
			StencilOp(GfxToGL::ToGLenum(sfail), GfxToGL::ToGLenum(dpfail), GfxToGL::ToGLenum(dppass));
		}

		inline void StencilOpBack(GfxOperations::StencilOp sfail, GfxOperations::StencilOp dpfail, GfxOperations::StencilOp dppass) {
			StencilOpBack(GfxToGL::ToGLenum(sfail), GfxToGL::ToGLenum(dpfail), GfxToGL::ToGLenum(dppass));
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

		inline void GetViewport(GfxTypes::Int* vp) noexcept {
			glGetIntegerv(GL_VIEWPORT, vp);
		}

		inline void SetViewport(const GfxTypes::Int* vp) noexcept {
			glViewport(vp[0], vp[1], vp[2], vp[3]);
		}
	};

#define gfxStates GfxStates::Instance()

// =================================================================================================
