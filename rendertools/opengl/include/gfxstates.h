#pragma once

#include <optional>
#include <tuple>
#include <utility>
#include <unordered_set>

#include "glew.h"
#include "gfxdrivertypes.h"
#include "rendertypes.h"

#include "array.hpp"
#include "string.hpp"
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
		GLenum lut[] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS, GL_LEQUAL };
		return lut[int(f)];
	}

	inline GLenum ToGLenum(BlendFactor f) noexcept {
		GLenum lut[] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR };
		return lut[int(f)];
	}

	inline GLenum ToGLenum(BlendOp op) noexcept {
		GLenum lut[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX };
		return lut[int(op)];
	}

	inline GLenum ToGLenum(CullFace c) noexcept {
		GLenum lut[] = { GL_FRONT, GL_BACK, GL_FRONT_AND_BACK };
		return lut[int(c)];
	}

	inline GLenum ToGLenum(Winding w) noexcept {
		return (w == Winding::Regular) ? GL_CW : GL_CCW;
	}

	inline GLenum ToGLenum(StencilOp op) noexcept {
		GLenum lut[] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INCR_WRAP, GL_DECR_WRAP };
		return lut[int(op)];
	}

	inline GLbitfield ToBufferMask(GLbitfield mask) noexcept {
		GLbitfield lut[] = { GL_COLOR_BUFFER_BIT, GL_DEPTH_BUFFER_BIT, GL_STENCIL_BUFFER_BIT };
		GLbitfield m = 0;
		GLbitfield f = GLbitfield(mask);
		for (int i = 0; f != 0; f <<= 1, ++i)
			if (f & 1)
				m |= lut[i];
		return m;
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
	RGBAColor m_clearColor{ ColorData::Invisible };
	float m_depthClearValue{ 1.0f };
	int m_stencilClearValue{ 0 };
	int m_featureLevel{ 0 };

	List<TextureSlotInfo> m_tmuBindings;
	List<RGBAColor> m_clearColorStack;

public:
	static constexpr int MinFeatureLevel = 330;
	static constexpr int SSBOFeatureLevel = 330; // actually 430; but NVidia 3.30 drivers support SSBOs, so we rely on querying GL_ARB_shader_storage_buffer_object

	GfxStates() {
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);
#if 0//def _DEBUG
		fprintf(stderr, "Max. texture size: %d\n", m_maxTextureSize);
#endif
		DetermineExtensions();
	}

	void DetermineExtensions(void);

	inline bool HasExtension(const char* extension) {
		return m_extensions.find(extension) != m_extensions.end();
	}

	inline int FeatureLevel(void) noexcept {
		if (m_featureLevel == 0) {
			const char* s = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
			m_featureLevel = int(float(String(s)) * 100);
		}
		return m_featureLevel;
	}

	inline bool HaveFeatureLevel(int level) noexcept {
		return FeatureLevel() >= (level ? level : MinFeatureLevel);
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

	inline int SetPolygonOffsetFill(GfxTypes::Float factor = 0.0f, GfxTypes::Float units = 0.0f) { 
		if (not SetState<GL_POLYGON_OFFSET_FILL>((factor * units == 0.0f) ? 0 : 1))
			return 0;
		glPolygonOffset(factor, units);
		return 1;
	}

	inline int SetDither(int state) { 
		return SetState<GL_DITHER>(state); 
	}

	inline int SetMultiSample(int state) { 
		return SetState<GL_MULTISAMPLE>(state); 
	}

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

	inline GfxOperations::CullFace CullFace(GfxOperations::CullFace mode) {
		CullFace(GfxToGL::ToGLenum(mode));
		return mode;
	}

	inline GfxOperations::Winding FrontFace(GfxOperations::Winding mode) {
		FrontFace(GfxToGL::ToGLenum(mode));
		return mode;
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

	inline void SetViewport(const GfxTypes::Int left, const GfxTypes::Int top, const GfxTypes::Int width, const GfxTypes::Int height) noexcept {
		glViewport(left, top, width, height);
	}

	template <typename T>
	inline void SetClearColor(T&& color)  noexcept {
		m_clearColor = std::forward<T>(color);
		glClearColor(m_clearColor.R(), m_clearColor.G(), m_clearColor.B(), m_clearColor.A());
	}

	inline void SetClearColor(float r, float g, float b, float a) {
		SetClearColor(RGBAColor(r, g, b, a));
	}

	inline RGBAColor GetClearColor(void) noexcept {
		return m_clearColor;
	}

	inline void ResetClearColor(void) noexcept {
		SetClearColor(ColorData::Invisible);
	}

	inline void ClearColorBuffers(void) noexcept {
		glClear(GL_COLOR_BUFFER_BIT);
	}

	inline void ClearDepthBuffer(GfxTypes::Float clearValue = 1.0f) {
		if (m_depthClearValue != clearValue) {
			m_depthClearValue = clearValue;
			glClearDepth(clearValue);
		}
		glClear(GL_DEPTH_BUFFER_BIT);
	}

	inline void ClearStencilBuffer(GfxTypes::Int clearValue = 0) {
		if (m_stencilClearValue != clearValue) {
			m_stencilClearValue = clearValue;
			glClearStencil(clearValue);
		}
		glClear(GL_STENCIL_BUFFER_BIT);
	}

	inline void PushClearColor(void) noexcept {
		m_clearColorStack.Push(m_clearColor);
	}


	inline void PopClearColor(void) noexcept {
		if (not m_clearColorStack.IsEmpty())
			m_clearColor = m_clearColorStack.Pop();
	}

	inline void SetMemoryBarrier(GLbitfield /*barriers*/) noexcept {
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	}

	using DrawBufferList = AutoArray<GfxTypes::Uint>;

	void SetDrawBuffers(const DrawBufferList& drawBuffers);

	void ClearBackBuffer(void);

	inline void Finish(void) noexcept {
		glFinish();
	}

	void ClearError(void) noexcept;

	bool CheckError(const char* operation = "") noexcept;
};

#define gfxStates GfxStates::Instance()

// =================================================================================================
