#pragma once

#include <array>
#include <tuple>
#include <cstring>
#include <cstdint>

#include "array.hpp"
#include "list.hpp"
#include "basesingleton.hpp"
#include "colordata.h"

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
        case TextureType::Texture3D: return GLenum(GL_TEXTURE_3D);
        case TextureType::CubeMap:   return GLenum(GL_TEXTURE_CUBE_MAP);
        default:                     return GLenum(GL_TEXTURE_2D);
    }
}

// =================================================================================================
// TextureSlotInfo: replaces TMUBindingInfo.
// Tracks the SRV descriptor-heap index (uint32_t) bound to each texture slot (0-based).
// Void*-handles are not used; the caller passes a uint32_t SRV index directly.

class TextureSlotInfo {
    static constexpr int MAX_SLOTS = 16;

    std::array<uint32_t, MAX_SLOTS> m_srvIndices{};
    int                          m_maxUsed{ 0 };
    GLenum                       m_typeTag{ GL_TEXTURE_2D };

public:
    explicit TextureSlotInfo(GLenum typeTag = GL_TEXTURE_2D);

    // Returns the slot index a given SRV is bound to, or -1 if not found.
    int  Find(uint32_t srvIndex) const noexcept;

    // Binds srvIndex to slotIndex (0-based). Picks the first free slot if slotIndex < 0.
    // Returns the effective slot index, or -1 on failure.
    int  Bind(uint32_t srvIndex, int slotIndex = -1) noexcept;

    // Releases the binding for srvIndex (optionally at a specific slot).
    // Returns true if anything was released.
    bool Release(uint32_t srvIndex, int slotIndex = -1) noexcept;

    // Returns the SRV index currently in slotIndex, or 0 if not set.
    uint32_t Query(int slotIndex) const noexcept;

    // Updates the recorded binding without side effects.
    bool Update(uint32_t srvIndex, int slotIndex) noexcept;

    inline GLenum GetTypeTag(void) const noexcept { return m_typeTag; }
};

// =================================================================================================
// RenderState: all PSO-relevant pipeline state encoded in a compact, hashable struct.
// Used as a cache key: hash(Shader* + RenderState) → ComPtr<ID3D12PipelineState>.
// Fields use GL enum values so callers need no changes; translation to D3D12 enums
// happens at PSO creation time.

struct RenderState {
    // Rasterizer
    GLenum   cullMode   { GL_BACK };       // GL_FRONT, GL_BACK, GL_FRONT_AND_BACK (=disabled)
    GLenum   frontFace  { GL_CW };         // GL_CW or GL_CCW
    // Depth-stencil
    uint8_t  depthTest  { 1 };
    uint8_t  depthWrite { 1 };
    GLenum   depthFunc  { GL_LEQUAL };
    uint8_t  stencilTest{ 0 };
    // Blend (RT0)
    uint8_t  blendEnable    { 0 };
    GLenum   blendSrcRGB    { GL_SRC_ALPHA };
    GLenum   blendDstRGB    { GL_ONE_MINUS_SRC_ALPHA };
    GLenum   blendSrcAlpha  { GL_SRC_ALPHA };
    GLenum   blendDstAlpha  { GL_ONE_MINUS_SRC_ALPHA };
    GLenum   blendOpRGB     { GL_FUNC_ADD };
    GLenum   blendOpAlpha   { GL_FUNC_ADD };
    // Color mask (bit0=R bit1=G bit2=B bit3=A)
    uint8_t  colorMask  { 0x0F };
    // Scissor
    uint8_t  scissorTest{ 0 };

    bool operator==(const RenderState& o) const noexcept {
        return std::memcmp(this, &o, sizeof(*this)) == 0;
    }
    bool operator!=(const RenderState& o) const noexcept { return !(*this == o); }
};

// =================================================================================================

class GfxStates : public BaseSingleton<GfxStates>
{
private:
    RenderState             m_state;
    bool                    m_stateDirty{ true };
    List<TextureSlotInfo>   m_slotInfos;
    int                     m_maxTextureSize{ 4096 };

public:
    GfxStates() = default;

    // Call once after DX12 device creation to set hardware limits.
    void Init(int maxTextureSize = 4096) noexcept { m_maxTextureSize = maxTextureSize; }

    // ---- PSO state access ----
    inline const RenderState& State(void) const noexcept { return m_state; }
    inline bool  IsStateDirty(void)  const noexcept { return m_stateDirty; }
    inline void  ClearStateDirty(void)     noexcept { m_stateDirty = false; }
    inline void  MarkStateDirty(void)      noexcept { m_stateDirty = true; }

    // ---- Capability queries (DX12 stubs) ----
    inline bool HasExtension(const char*) const noexcept { return false; }
    inline int  MaxTextureSize(void)      const noexcept { return m_maxTextureSize; }

    // ---- State setters — same names and semantics as the OpenGL version ----
    // Passing state < 0 (via Tristate) is a query-only call; the state is returned unchanged.
    // All setters mark the state dirty when the value changes.

    inline int SetDepthTest(int state) {
        int prev = int(m_state.depthTest);
        if (state >= 0 && uint8_t(state) != m_state.depthTest) {
            m_state.depthTest = uint8_t(state);
            m_stateDirty = true;
        }
        return prev;
    }

    inline int SetDepthWrite(int state) {
        int prev = int(m_state.depthWrite);
        if (state >= 0 && uint8_t(state) != m_state.depthWrite) {
            m_state.depthWrite = uint8_t(state);
            m_stateDirty = true;
        }
        return prev;
    }

    inline int SetBlending(int state) {
        int prev = int(m_state.blendEnable);
        if (state >= 0 && uint8_t(state) != m_state.blendEnable) {
            m_state.blendEnable = uint8_t(state);
            m_stateDirty = true;
        }
        return prev;
    }

    inline int SetFaceCulling(int state) {
        // 0 = disable culling (GL_FRONT_AND_BACK), 1 = cull GL_BACK
        int prev = (m_state.cullMode != GL_FRONT_AND_BACK) ? 1 : 0;
        if (state >= 0) {
            GLenum newMode = state ? GLenum(GL_BACK) : GLenum(GL_FRONT_AND_BACK);
            if (newMode != m_state.cullMode) {
                m_state.cullMode = newMode;
                m_stateDirty = true;
            }
        }
        return prev;
    }

    inline int SetScissorTest(int state) {
        int prev = int(m_state.scissorTest);
        if (state >= 0 && uint8_t(state) != m_state.scissorTest) {
            m_state.scissorTest = uint8_t(state);
            m_stateDirty = true;
        }
        return prev;
    }

    inline int SetStencilTest(int state) {
        int prev = int(m_state.stencilTest);
        if (state >= 0 && uint8_t(state) != m_state.stencilTest) {
            m_state.stencilTest = uint8_t(state);
            m_stateDirty = true;
        }
        return prev;
    }

    // No-ops for states that don't map to DX12 PSO (kept for source compatibility).
    inline int SetPolygonOffsetFill(int) noexcept { return 0; }
    inline int SetDither(int)            noexcept { return 0; }
    inline int SetMultiSample(int)       noexcept { return 0; }

    inline GLenum DepthFunc(GLenum state) {
        GLenum prev = m_state.depthFunc;
        if (state != GLenum(GL_NONE) && state != prev) {
            m_state.depthFunc = state;
            m_stateDirty = true;
        }
        return prev;
    }

    inline GLenum BlendEquation(GLenum state) {
        GLenum prev = m_state.blendOpRGB;
        if (state != GLenum(GL_NONE) && state != prev) {
            m_state.blendOpRGB   = state;
            m_state.blendOpAlpha = state;
            m_stateDirty = true;
        }
        return prev;
    }

    inline GLenum FrontFace(GLenum state) {
        GLenum prev = m_state.frontFace;
        if (state != GLenum(GL_NONE) && state != prev) {
            m_state.frontFace = state;
            m_stateDirty = true;
        }
        return prev;
    }

    inline GLenum CullFace(GLenum state) {
        GLenum prev = m_state.cullMode;
        if (state != GLenum(GL_NONE) && state != prev) {
            m_state.cullMode = state;
            m_stateDirty = true;
        }
        return prev;
    }

    inline GLenum ActiveTexture(GLenum) noexcept { return 0u; } // no-op in DX12

    inline std::tuple<GLenum, GLenum> BlendFunc(GLenum sFactor, GLenum dFactor) {
        auto prev = std::make_tuple(m_state.blendSrcRGB, m_state.blendDstRGB);
        if (sFactor != m_state.blendSrcRGB || dFactor != m_state.blendDstRGB
            || sFactor != m_state.blendSrcAlpha || dFactor != m_state.blendDstAlpha) {
            m_state.blendSrcRGB   = sFactor;
            m_state.blendDstRGB   = dFactor;
            m_state.blendSrcAlpha = sFactor;
            m_state.blendDstAlpha = dFactor;
            m_stateDirty = true;
        }
        return prev;
    }

    inline std::tuple<GLenum, GLenum, GLenum, GLenum>
    BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
        auto prev = std::make_tuple(m_state.blendSrcRGB, m_state.blendDstRGB,
                                    m_state.blendSrcAlpha, m_state.blendDstAlpha);
        if (srcRGB != m_state.blendSrcRGB || dstRGB != m_state.blendDstRGB
            || srcAlpha != m_state.blendSrcAlpha || dstAlpha != m_state.blendDstAlpha) {
            m_state.blendSrcRGB   = srcRGB;
            m_state.blendDstRGB   = dstRGB;
            m_state.blendSrcAlpha = srcAlpha;
            m_state.blendDstAlpha = dstAlpha;
            m_stateDirty = true;
        }
        return prev;
    }

    inline std::tuple<float, float, float, float> ClearColor(float r, float g, float b, float a) {
        static float cr = 0, cg = 0, cb = 0, ca = 0;
        auto prev = std::make_tuple(cr, cg, cb, ca);
        cr = r; cg = g; cb = b; ca = a;
        return prev;
    }

    inline RGBAColor ClearColor(RGBAColor color) {
        auto t = ClearColor(color.R(), color.G(), color.B(), color.A());
        return RGBAColor{ std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t) };
    }

    inline std::tuple<bool, bool, bool, bool> ColorMask(bool r, bool g, bool b, bool a) {
        uint8_t prev = m_state.colorMask;
        uint8_t next = uint8_t((r ? 1u : 0u) | (g ? 2u : 0u) | (b ? 4u : 0u) | (a ? 8u : 0u));
        if (next != prev) {
            m_state.colorMask = next;
            m_stateDirty = true;
        }
        return { bool(prev & 1u), bool(prev & 2u), bool(prev & 4u), bool(prev & 8u) };
    }

    // ---- Texture slot management ----
    // In the DX12 version the "handle" parameter is a SRV descriptor-heap index (uint32_t),
    // not a GL texture name. Callers in DX12-specific code pass the SRV index directly.
    // The type-tag (GL_TEXTURE_2D etc.) is kept for conceptual grouping only.

    TextureSlotInfo* FindInfo(GLenum typeTag);

    int  BoundTMU(GLenum typeTag, uint32_t srvIndex, int slotIndex = -1);
    int  BindTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex);
    bool ReleaseTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex = -1);
    int  GetBoundTexture(GLenum typeTag, int slotIndex);
    int  SetBoundTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex);

    // Typed convenience wrappers
    template<GLenum typeTag>
    inline bool BindTexture(uint32_t srvIndex, int slotIndex) {
        return BindTexture(typeTag, srvIndex, slotIndex) >= 0;
    }
    inline bool BindTexture2D(uint32_t srvIndex, int slotIndex)  { return BindTexture<GL_TEXTURE_2D>(srvIndex, slotIndex); }
    inline bool BindCubemap(uint32_t srvIndex, int slotIndex)    { return BindTexture<GL_TEXTURE_CUBE_MAP>(srvIndex, slotIndex); }

    void ReleaseBuffers(void) noexcept;
};

#define gfxStates GfxStates::Instance()

// =================================================================================================
