#pragma once

#include <array>
#include <tuple>
#include <cstring>
#include <cstdint>

#include "vkframework.h"
#include "vkcontext.h"
#include "image_layout_tracker.h"
#include "array.hpp"
#include "list.hpp"
#include "dictionary.hpp"
#include "basesingleton.hpp"
#include "colordata.h"
#include "renderstates.h"

// =================================================================================================
// GL compatibility type — defined here so code that uses Tristate<GLenum> compiles unchanged.
// Guard prevents conflict if glew.h is included in the same translation unit (glew defines
// the same type as a typedef).
#ifndef GL_VERSION_1_0
using GLenum = unsigned int;
#endif

// =================================================================================================
// GL compat constants — same numeric values as OpenGL so existing callers compile unchanged.
// At PSO creation time these are translated to D3D12_* equivalents.

#ifndef GL_NONE
#   define GL_NONE                      0u
#endif
#ifndef GL_FALSE
#   define GL_FALSE                     0
#endif
#ifndef GL_TRUE
#   define GL_TRUE                      1
#endif
// Blend factors
#ifndef GL_ZERO
#   define GL_ZERO                      0u
#endif
#ifndef GL_ONE
#   define GL_ONE                       1u
#endif
#ifndef GL_SRC_COLOR
#   define GL_SRC_COLOR                 0x0300u
#endif
#ifndef GL_ONE_MINUS_SRC_COLOR
#   define GL_ONE_MINUS_SRC_COLOR       0x0301u
#endif
#ifndef GL_SRC_ALPHA
#   define GL_SRC_ALPHA                 0x0302u
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#   define GL_ONE_MINUS_SRC_ALPHA       0x0303u
#endif
#ifndef GL_DST_ALPHA
#   define GL_DST_ALPHA                 0x0304u
#endif
#ifndef GL_ONE_MINUS_DST_ALPHA
#   define GL_ONE_MINUS_DST_ALPHA       0x0305u
#endif
#ifndef GL_DST_COLOR
#   define GL_DST_COLOR                 0x0306u
#endif
#ifndef GL_ONE_MINUS_DST_COLOR
#   define GL_ONE_MINUS_DST_COLOR       0x0307u
#endif
// Blend equations
#ifndef GL_FUNC_ADD
#   define GL_FUNC_ADD                  0x8006u
#endif
#ifndef GL_FUNC_SUBTRACT
#   define GL_FUNC_SUBTRACT             0x800Au
#endif
#ifndef GL_FUNC_REVERSE_SUBTRACT
#   define GL_FUNC_REVERSE_SUBTRACT     0x800Bu
#endif
#ifndef GL_MIN
#   define GL_MIN                       0x8007u
#endif
#ifndef GL_MAX
#   define GL_MAX                       0x8008u
#endif
// Depth / comparison functions
#ifndef GL_NEVER
#   define GL_NEVER                     0x0200u
#endif
#ifndef GL_LESS
#   define GL_LESS                      0x0201u
#endif
#ifndef GL_EQUAL
#   define GL_EQUAL                     0x0202u
#endif
#ifndef GL_LEQUAL
#   define GL_LEQUAL                    0x0203u
#endif
#ifndef GL_GREATER
#   define GL_GREATER                   0x0204u
#endif
#ifndef GL_NOTEQUAL
#   define GL_NOTEQUAL                  0x0205u
#endif
#ifndef GL_GEQUAL
#   define GL_GEQUAL                    0x0206u
#endif
#ifndef GL_ALWAYS
#   define GL_ALWAYS                    0x0207u
#endif
// Face culling
#ifndef GL_FRONT
#   define GL_FRONT                     0x0404u
#endif
#ifndef GL_BACK
#   define GL_BACK                      0x0405u
#endif
#ifndef GL_FRONT_AND_BACK
#   define GL_FRONT_AND_BACK            0x0408u
#endif
// Winding
#ifndef GL_CW
#   define GL_CW                        0x0900u
#endif
#ifndef GL_CCW
#   define GL_CCW                       0x0901u
#endif
// Stencil operations
#ifndef GL_KEEP
#   define GL_KEEP                      0x1E00u
#endif
#ifndef GL_REPLACE
#   define GL_REPLACE                   0x1E01u
#endif
#ifndef GL_INCR
#   define GL_INCR                      0x1E02u
#endif
#ifndef GL_DECR
#   define GL_DECR                      0x1E03u
#endif
#ifndef GL_INCR_WRAP
#   define GL_INCR_WRAP                 0x8507u
#endif
#ifndef GL_DECR_WRAP
#   define GL_DECR_WRAP                 0x8508u
#endif
// Texture wrap modes
#ifndef GL_REPEAT
#   define GL_REPEAT                    0x2901u
#endif
#ifndef GL_CLAMP_TO_EDGE
#   define GL_CLAMP_TO_EDGE             0x812Fu
#endif
#ifndef GL_MIRRORED_REPEAT
#   define GL_MIRRORED_REPEAT           0x8370u
#endif
// Texture types (used as type tags)
#ifndef GL_TEXTURE_2D
#   define GL_TEXTURE_2D                0x0DE1u
#endif
#ifndef GL_TEXTURE_3D
#   define GL_TEXTURE_3D                0x806Fu
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#   define GL_TEXTURE_CUBE_MAP          0x8513u
#endif

// =================================================================================================
// Map API-neutral TextureType to the GLenum tag used as slot-group key in the state tracker.
#include "rendertypes.h"

inline GLenum TextureTypeToGLenum(TextureType t) noexcept {
    switch (t) {
    case TextureType::Texture3D:
        return GLenum(GL_TEXTURE_3D);
    case TextureType::CubeMap:
        return GLenum(GL_TEXTURE_CUBE_MAP);
    default:
        return GLenum(GL_TEXTURE_2D);
    }
}

// =================================================================================================
// TextureSlotInfo: tracks the SRV descriptor-heap index (uint32_t) bound to each texture slot.

class TextureSlotInfo {
    static constexpr int MAX_SLOTS = 16;

    std::array<uint32_t, MAX_SLOTS> m_srvIndices{};
    int                          m_maxUsed{ 0 };
    GLenum                       m_typeTag{ GL_TEXTURE_2D };

public:
    explicit TextureSlotInfo(GLenum typeTag = GL_TEXTURE_2D);

    int  Find(uint32_t srvIndex) const noexcept;
    int  Bind(uint32_t srvIndex, int slotIndex = -1) noexcept;
    bool Release(uint32_t srvIndex, int slotIndex = -1) noexcept;
    uint32_t Query(int slotIndex) const noexcept;
    bool Update(uint32_t srvIndex, int slotIndex) noexcept;

    inline GLenum GetTypeTag(void) const noexcept { return m_typeTag; }
};

// =================================================================================================

class GfxStates
    : public BaseSingleton<GfxStates>
{
private:
    RenderStates            m_renderStates;
    List<TextureSlotInfo>   m_slotInfos;
    GfxTypes::Int           m_viewport[4];
    int                     m_maxTextureSize{ 4096 };
    RGBAColor               m_clearColor{ ColorData::Invisible };
    List<RGBAColor>         m_clearColorStack;
    int                     m_featureLevel{ 0 };

    RenderStates& ActiveState(void) noexcept;

public:
    // FeatureLevel maps onto Vulkan's encoded API version (VK_MAKE_API_VERSION).
    // MinFeatureLevel = 1.3 because the port targets dynamic rendering + synchronization2 as core.
    // SSBOFeatureLevel stays at 1.0 — storage buffers have been core since 1.0; the call site
    // only checks "is the platform new enough at all".
    static constexpr int MinFeatureLevel = (int)VK_API_VERSION_1_3;
    static constexpr int SSBOFeatureLevel = (int)VK_API_VERSION_1_0;

    GfxStates() = default;

    void Init(int maxTextureSize = 4096) noexcept {
        m_maxTextureSize = maxTextureSize;
    }

    inline int FeatureLevel(void) noexcept {
        if (m_featureLevel == 0)
            m_featureLevel = (int)vkContext.ApiVersion();
        return m_featureLevel;
    }

    inline bool HaveFeatureLevel(int minLevel) noexcept {
        return FeatureLevel() >= minLevel;
    }

    inline const RenderStates& State(void) noexcept {
        return ActiveState();
    }

    inline bool HasExtension(const char*) const noexcept {
        return false;
    }

    inline int MaxTextureSize(void) const noexcept {
        return m_maxTextureSize;
    }

    inline int SetDepthTest(int state) {
        auto& s = ActiveState();
        int prevState = int(s.depthTest);
        if ((state >= 0) and (uint8_t(state) != s.depthTest))
            s.depthTest = uint8_t(state);
        return prevState;
    }

    inline int SetDepthWrite(int state) {
        auto& s = ActiveState();
        int prevState = int(s.depthWrite);
        s.depthWrite = uint8_t(state);
        return prevState;
    }

    inline int SetBlending(int state) {
        auto& s = ActiveState();
        int prevState = int(s.blendEnable);
        s.blendEnable = uint8_t(state);
        return prevState;
    }

    inline int SetDepthClip(int state) {
        auto& s = ActiveState();
        int prevState = int(s.depthClip);
        s.depthClip = uint8_t(state);
        return prevState;
    }

    inline int SetFaceCulling(int state) {
        auto& s = ActiveState();
        int prevState = (s.cullMode != GfxOperations::CullFace::None) ? 1 : 0;
        s.cullMode = state ? GfxOperations::CullFace::Back : GfxOperations::CullFace::None;
        return prevState;
    }

    inline int SetScissorTest(int state) {
        auto& s = ActiveState();
        int prevState = int(s.scissorTest);
        s.scissorTest = uint8_t(state);
        return prevState;
    }

    inline int SetStencilTest(int state) {
        auto& s = ActiveState();
        int prevState = int(s.stencilTest);
        s.stencilTest = uint8_t(state);
        return prevState;
    }

    inline void StencilFunc(GfxOperations::CompareFunc func, uint8_t ref, uint8_t mask) {
        auto& s = ActiveState();
        s.stencilFunc = func;
        s.stencilRef = ref;
        s.stencilMask = mask;
    }

    inline void StencilOp(GfxOperations::StencilOp sfail, GfxOperations::StencilOp dpfail, GfxOperations::StencilOp dppass) {
        auto& s = ActiveState();
        s.stencilSFail = sfail;
        s.stencilDPFail = dpfail;
        s.stencilDPPass = dppass;
    }

    inline void StencilOpBack(GfxOperations::StencilOp sfail, GfxOperations::StencilOp dpfail, GfxOperations::StencilOp dppass) {
        auto& s = ActiveState();
        s.stencilBackSFail = sfail;
        s.stencilBackDPFail = dpfail;
        s.stencilBackDPPass = dppass;
    }

    inline int SetPolygonOffsetFill(int) noexcept {
        return 0;
    }

    inline int SetDither(int) noexcept {
        return 0;
    }

    inline int SetMultiSample(int) noexcept {
        return 0;
    }

    inline GfxOperations::CompareFunc DepthFunc(GfxOperations::CompareFunc state) {
        auto& s = ActiveState();
        auto prevState = s.depthFunc;
        s.depthFunc = state;
        return prevState;
    }

    inline GfxOperations::BlendOp BlendEquation(GfxOperations::BlendOp state) {
        auto& s = ActiveState();
        auto prevState = s.blendOpRGB;
        s.blendOpRGB = state;
        s.blendOpAlpha = state;
        return prevState;
    }

    inline GfxOperations::Winding FrontFace(GfxOperations::Winding state) {
        auto& s = ActiveState();
        auto prevState = s.winding;
        s.winding = state;
        return prevState;
    }

    inline GfxOperations::CullFace CullFace(GfxOperations::CullFace state) {
        auto& s = ActiveState();
        auto prevState = s.cullMode;
        s.cullMode = state;
        return prevState;
    }

    // OGL glPolygonOffset equivalent: factor -> SlopeScaledDepthBias, units -> DepthBias.
    inline void SetPolygonOffset(float factor, float units) {
        auto& s = ActiveState();
        s.slopeScaledDepthBias = factor;
        s.depthBias = int32_t(units);
    }

    inline void BlendFunc(GfxOperations::BlendFactor src, GfxOperations::BlendFactor dst) {
        auto& s = ActiveState();
        s.blendSrcRGB = src;
        s.blendDstRGB = dst;
        s.blendSrcAlpha = src;
        s.blendDstAlpha = dst;
    }

    inline void BlendFuncSeparate(GfxOperations::BlendFactor srcRGB, GfxOperations::BlendFactor dstRGB,
        GfxOperations::BlendFactor srcAlpha, GfxOperations::BlendFactor dstAlpha) {
        auto& s = ActiveState();
        s.blendSrcRGB = srcRGB;
        s.blendDstRGB = dstRGB;
        s.blendSrcAlpha = srcAlpha;
        s.blendDstAlpha = dstAlpha;
    }

    inline RGBAColor ClearColor(RGBAColor color) {
        RGBAColor prev = m_clearColor;
        m_clearColor = color;
        return prev;
    }

    template <typename T>
    inline void SetClearColor(T&& color) noexcept {
        m_clearColor = std::forward<T>(color);
    }

    inline void SetClearColor(float r, float g, float b, float a) {
        m_clearColor = RGBAColor(r, g, b, a);
    }

    inline RGBAColor GetClearColor(void) noexcept {
        return m_clearColor;
    }

    inline void ResetClearColor(void) noexcept {
        m_clearColor = ColorData::Invisible;
    }

    inline void PushClearColor(void) noexcept {
        m_clearColorStack.Push(m_clearColor);
    }

    inline void PopClearColor(void) noexcept {
        if (not m_clearColorStack.IsEmpty())
            m_clearColor = m_clearColorStack.Pop();
    }

    inline std::tuple<bool, bool, bool, bool> ColorMask(bool r, bool g, bool b, bool a) {
        auto& s = ActiveState();
        uint8_t prevState = s.colorMask;
        s.colorMask = uint8_t((r ? 1u : 0u) | (g ? 2u : 0u) | (b ? 4u : 0u) | (a ? 8u : 0u));;
        return { bool(prevState & 1u), bool(prevState & 2u), bool(prevState & 4u), bool(prevState & 8u) };
    }

    TextureSlotInfo* FindInfo(GLenum typeTag);

    int  BoundTMU(GLenum typeTag, uint32_t srvIndex, int slotIndex = -1);

    int  BindTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex);

    bool ReleaseTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex = -1);

    int  GetBoundTexture(GLenum typeTag, int slotIndex);

    int  SetBoundTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex);

    template<GLenum typeTag>
    inline bool BindTexture(uint32_t srvIndex, int slotIndex) {
        return BindTexture(typeTag, srvIndex, slotIndex) >= 0;
    }

    inline bool BindTexture2D(uint32_t srvIndex, int slotIndex) {
        return BindTexture<GL_TEXTURE_2D>(srvIndex, slotIndex);
    }

    inline bool BindCubemap(uint32_t srvIndex, int slotIndex) {
        return BindTexture<GL_TEXTURE_CUBE_MAP>(srvIndex, slotIndex);
    }

    // Vulkan signature: VkImage + ImageLayoutTracker replace the DX12 descriptor handle. The caller
    // (RenderTarget / Swapchain) owns the tracker. The clear runs outside any RenderPass via
    // vkCmdClearColorImage / vkCmdClearDepthStencilImage; the tracker is transitioned to
    // TRANSFER_DST_OPTIMAL and left there (caller transitions to COLOR_ATTACHMENT/DEPTH_ATTACHMENT
    // before the next draw).
    void ClearColorBuffers(VkImage image, ImageLayoutTracker& tracker) noexcept;

    void ClearBackBuffer(const RGBAColor& color = ColorData::Invisible) noexcept;

    void ClearDepthBuffer(VkImage image, ImageLayoutTracker& tracker, float clearValue = 1.0f) noexcept;

    void ClearStencilBuffer(VkImage image, ImageLayoutTracker& tracker, int clearValue = 0) noexcept;

    void SetMemoryBarrier(GfxTypes::Bitfield barriers = 0) noexcept;

    void Finish(void) noexcept;

    void ReleaseBuffers(void) noexcept;

    // DX12: no GPU-readable viewport state; viewport is tracked by the application.
    void GetViewport(GfxTypes::Int* viewport) noexcept {
        std::memcpy(viewport, m_viewport, sizeof(m_viewport));
    }

    inline void SetViewport(const GfxTypes::Int* vp) noexcept {
        SetViewport(vp[0], vp[1], vp[2], vp[3]);
    }

    inline void SetViewport(void) noexcept {
        SetViewport(m_viewport);
    }

    void SetViewport(const GfxTypes::Int left, const GfxTypes::Int top, const GfxTypes::Int right, const GfxTypes::Int bottom) noexcept;

    using DrawBufferList = AutoArray <GfxTypes::Uint>;

    void SetDrawBuffers(const DrawBufferList& drawBuffers);


    void ClearError(void) noexcept;

    bool CheckError(const char* operation = "") noexcept;
};

#define gfxStates GfxStates::Instance()

// =================================================================================================
