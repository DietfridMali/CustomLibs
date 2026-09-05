#pragma once

#include "glew.h"

#include "array.hpp"
#include "array.hpp"
#include "viewport.h"
#include "texture.h"
#include "colordata.h"
#include "drawbufferhandler.h"
#include "gfxpixelformat_gl.h"	// ToNativeColorFormat () for RTCreationParams::colorFormat - backend neutral
#ifdef _DEBUG
#   include <source_location>
#endif

// =================================================================================================

#define INVALID_BUFFER_INDEX 0x80000000

class BufferInfo {
public:
    typedef enum {
        btColor,
        btDepth,
        btStencil, // never created as a buffer of its own -- stencil is a plane of btDepth (see RenderTarget::m_stencilBufferIndex)
        btVertex,
        btSkyMap, // RGBA16F texture for compute write (image2D); not attached to FBO
        // A cube map rendered INTO, one face at a time - see SelectCubeFace (). Six faces in one
        // resource, so a shader can sample it by direction afterwards. What it is for: anything that
        // has to capture its surroundings in every direction from one point, an omnidirectional shadow
        // map above all.
        btCubemap
    } eBufferType;


    SharedTextureHandle m_handle;
    int                 m_attachment;
    // Where the buffer is attached RIGHT NOW. Normally m_attachment, but dbSingle moves the selected
    // colour buffer onto GL_COLOR_ATTACHMENT0 (see RenderTarget::SelectDrawBuffers), and a detach has
    // to address the point the texture actually sits on, not the one it belongs to.
    int                 m_boundAttachment;
    int                 m_tmuIndex;
    eBufferType         m_type;
    bool                m_isAttached;

    BufferInfo(GLuint handle = 0, int attachment = 0)
        : m_handle(handle), m_attachment(attachment), m_boundAttachment(GL_NONE), m_tmuIndex(-1), m_type(btColor), m_isAttached(false)
    {}

    void Init(void) {
        m_handle = SharedTextureHandle(0);
        m_attachment = 0;
        m_boundAttachment = GL_NONE;
        m_tmuIndex = -1;
        m_type = btColor;
        m_isAttached = false;
    }
};

// =================================================================================================

class RenderTarget {
public:
    using DrawBufferList = DrawBufferHandler::DrawBufferList;
    using CustomDrawBufferList = DrawBufferHandler::CustomDrawBufferList;

    typedef enum {
        dbAll,
        dbColor,
        dbExtra,
        dbSingle,
        dbCustom,
        dbDepth,
        dbCount,
        dbNone = -1
    } eDrawBufferGroups;

    // Shared API with the DX backend (read-only/sampleable depth for soft particles etc.). OpenGL has no
    // separate depth views: dbmReadOnly turns depth (and stencil) writes off for the pass, which is exactly
    // what makes the attached depth buffer safe to sample as a texture at the same time.
    typedef enum {
        dbmWrite,
        dbmReadOnly
    } eDepthBufferMode;

    String                      m_name;
    SharedFramebufferHandle     m_handle;
    int                         m_width;
    int                         m_height;
    int                         m_scale;
    int                         m_bufferCount;
    int                         m_colorBufferCount;
    GLenum                      m_colorFormat{ GL_RGBA8 };
    GLenum                      m_cubeMapFormat{ GL_R32F };
    int                         m_extraBufferCount;
    int                         m_extraBufferIndex;
    int                         m_depthBufferIndex;
    // A stencil buffer is never a buffer of its own: the hardware stores depth and stencil interleaved,
    // and DX12 has no pure stencil format at all. stencilBufferCount > 0 therefore gives the DEPTH buffer
    // a stencil plane (GL_DEPTH32F_STENCIL8 instead of GL_DEPTH_COMPONENT32F), and m_stencilBufferIndex
    // is just an alias of m_depthBufferIndex. Without it the depth buffer stays 32 bit.
    int                         m_stencilBufferIndex;
    bool                        m_hasStencil{ false };
    int                         m_computeBufferIndex{ -1 };   // start of compute-buffer slot range in m_bufferInfo
    int                         m_computeBufferCount{ 0 };
    int                         m_cubeMapIndex{ -1 };         // start of cube-map slot range in m_bufferInfo
    int                         m_cubeMapCount{ 0 };
    int                         m_cubeFace{ 0 };              // face currently attached, see SelectCubeFace
    int                         m_activeBufferIndex;
    AutoArray<BufferInfo>       m_bufferInfo;
    DrawBufferList              m_drawBuffers;
    CustomDrawBufferList        m_customDrawBuffers;   // see SelectCustomDrawBuffers
    Viewport                    m_viewport;
    Viewport* m_viewportSave;
    // How this target's colour buffer is sampled when it is read back as a texture. One that gets
    // rescaled on the way out (post processing) wants LINEAR; one that is read texel for texel - a
    // TextureAtlas, whose cells sit flush against each other - must not be filtered at all. Only the
    // owner knows which of the two it is, so RenderTargetTexture::SetParams () takes it from here.
    GfxFilterMode               m_filtering{ GfxFilterMode::Linear };
    // One wrapper PER COLOUR BUFFER, not one for the target. A single wrapper had its handle rehung on
    // every GetAsTexture () call, so two colour buffers of the same target came back as the same
    // pointer carrying the second one's handle - a draw that wants both at once (an atlas with a
    // diffuse and an ambient layer, say) could not be served at all.
    // Sized once in Create (), from colorBufferCount - it must never grow afterwards: the array is a
    // std::vector and would move its elements, while their addresses are what GetAsTexture () hands out.
    AutoArray<RenderTargetTexture> m_renderTextures;
    // For a source from OUTSIDE this target (params.source < 0). Kept apart from the list above: it is
    // not one of this target's buffers, and it must not be wrapped as an owned handle.
    RenderTargetTexture         m_externalTexture;
    RenderTargetTexture         m_depthTexture;
    ShadowTexture               m_shadowTexture; // ShadowTexture for sampler2DShadow and HW 2x2 PCF; requires changes in a few shaders
    bool                        m_pingPong;
    bool                        m_isAvailable;
    bool                        m_wasActivated;
    bool                        m_isScreenBuffer;
    int                         m_lastDestination;
    BaseQuadMesh                    m_viewportArea;
    eDrawBufferGroups           m_drawBufferGroup;
    // Depth mode of the current activation, so a Reactivate does not silently drop back to a writable
    // depth buffer while a pass is still sampling it.
    eDepthBufferMode            m_depthMode{ dbmWrite };
    RGBAColor                   m_clearColor;
    RenderTarget*               m_depthSource{ nullptr };   // foreign depth buffer attached instead of an own one (SetDepthSource)

    static GLint                m_activeHandle;

    struct RTCreationParams {
        String name{ "" };
        int colorBufferCount{ 1 };
        GLenum colorFormat{ GL_RGBA8 };
        int depthBufferCount{ 0 };
        int stencilBufferCount{ 0 };
        int vertexBufferCount{ 0 };
        int skyMapCount{ 0 };  // Compute-only storage textures (RGBA16F image2D), no FBO attachment.
        // Cube maps to render into (btCubemap). Their edge length is the target's width, so a cube map
        // target is square by definition. cubeMapFormat is separate from colorFormat because the two
        // rarely want the same thing - a shadow cube map holds one distance per texel, a colour target
        // holds RGBA.
        int cubeMapCount{ 0 };
        GLenum cubeMapFormat{ GL_R32F };
        bool hasMRTs{ false };
        bool isScreenBuffer{ false };
        bool storageImage{ false };   // Cross-API; honored only by Vulkan today.
    };

    struct RTRenderParams {
        int source{ 0 };
        int destination{ -1 };
        bool clearBuffer{ true };
        bool premultiply{ false };
        int flipVertically{ 0 }; // -1: flip, 1: don't flip, 0: renderer decides
        bool centerOrigin{ true };
        float rotation{ 0.0f };
        float scale{ 1.0f };
        Shader* shader{ nullptr };
    };

    struct RTActivationParams {
        int bufferIndex{ -1 };
        eDrawBufferGroups drawBufferGroup{ dbAll };
        bool clear{ true };
        bool reactivate{ false };
        eDepthBufferMode depthMode{ dbmWrite };
    };

    RenderTarget();

    ~RenderTarget() {
        Destroy();
    }

    void Init(void);

    bool Create(int width, int height, int scale, const RTCreationParams& params);

    void Destroy(void);

    bool IsActive(void) noexcept;

#ifdef _DEBUG
    bool Activate(const RTActivationParams& params, const std::source_location& loc = std::source_location::current());
#else
    bool Activate(const RTActivationParams& params);
#endif

    bool EnableBuffers(const RTActivationParams& params);

    bool SelectDrawBuffers(const RTActivationParams& params);

    inline bool Reactivate(bool clear = false) {
        RTActivationParams params{ .bufferIndex = m_activeBufferIndex, .drawBufferGroup = m_drawBufferGroup, .clear = clear, .reactivate = true, .depthMode = m_depthMode };
        return Activate(params);
    }

    void Deactivate(void) noexcept;

    // No-op on OGL: render-pass scope pause/resume is a Vulkan-only concept (vkCmdBeginRendering /
    // vkCmdEndRendering). In OGL the framebuffer binding is global state and does not need to be
    // suspended around UAV clears or copy ops. Present for source-level compatibility with the
    // Vulkan path; common-code callers (e.g. DecalHandler::Render) use it unconditionally.
    inline void BeginRendering(bool /*clearColor*/ = false, bool /*clearDepth*/ = false) noexcept {}
    inline void EndRendering(void) noexcept {}

    bool Enable(const RTActivationParams& params);

    void Disable(bool deactivate = true) noexcept;

    inline void Flush(void) noexcept {
        // no op
    }

    void SetViewport(bool flipVertically = false)
        noexcept;

    void Fill(RGBAColor color);

    void Clear(const RTActivationParams& params);

    // Share another render target's depth buffer: attaches the source's depth texture to this FBO's
    // depth slot (persistent FBO state -- done once here, every Activate sees it). The foreign depth
    // is never cleared, because Clear/ClearDepthBuffer gate on an OWN depth buffer (HaveDepthBuffer),
    // which this target does not have; it is meant to be tested against, not written (leave depth
    // write off). Lets an overlay pass (e.g. the wet-splat decal buffer) hardware-depth-test against
    // the scene. Pass nullptr to detach again.
    void SetDepthSource(RenderTarget* source);

    Texture* GetAsTexture(const RTRenderParams& params, int tmuIndex = 0);

    // One colour buffer's texels into a CPU buffer, in the target's own colour format. bufferSize is
    // the size of the destination in BYTES and is checked against BufferSize (), so a buffer that is
    // too small is refused rather than overrun.
    //
    // This DRAINS THE PIPELINE: everything queued has to finish before the texels can be handed over.
    // It is meant for saving a baked result to disk or for a diagnosis, never for something that runs
    // per frame.
    bool ReadBuffer(int bufferIndex, void* buffer, size_t bufferSize);

    // The other direction: CPU texels INTO one colour buffer, in the target's own colour format.
    // dataSize is checked against BufferSize () the same way ReadBuffer () checks its destination.
    // Used to restore a buffer that was saved earlier - a baked lightmap read back from a file.
    bool WriteBuffer(int bufferIndex, const void* data, size_t dataSize);

    // Bytes one colour buffer occupies, at the target's scaled size and its colour format.
    size_t BufferSize(int bufferIndex);

    Texture* GetDepthAsTexture(void);

    Texture* GetDepthAsShadowTexture(void);

    bool UpdateTransformation(const RTRenderParams& params);

    bool RenderAsTexture(Texture* texture, const RTRenderParams& params, const RGBAColor& color);

    inline bool RenderAsTexture(Texture* texture, const RTRenderParams& params, RGBAColor&& color) {
        return RenderAsTexture(texture, params, static_cast<const RGBAColor&>(color));
    }

    inline bool RenderAsTexture(Texture* texture, const RTRenderParams& params) {
        return RenderAsTexture(texture, params, ColorData::White);
    }

    bool Render(const RTRenderParams& params, const RGBAColor& color);

    inline bool Render(const RTRenderParams& params, RGBAColor&& color) {
        return Render(params, static_cast<const RGBAColor&>(color));
    }

    inline bool Render(const RTRenderParams& params) {
        return Render(params, ColorData::White);
    }

    bool AutoRender(const RTRenderParams& params, const RGBAColor& color);

    bool AutoRender(const RTRenderParams& params, RGBAColor&& color) {
        return AutoRender(params, static_cast<const RGBAColor&>(color));
    }

    bool AutoRender(const RTRenderParams& params) {
        return AutoRender(params, ColorData::White);
    }

    inline int GetWidth(bool scaled = false) {
        return scaled ? m_width * m_scale : m_width;
    }

    inline int GetHeight(bool scaled = false) {
        return scaled ? m_height * m_scale : m_height;
    }

    inline int GetScale(void) noexcept {
        return m_scale;
    }

    // Texel size of THIS render target's own texture (1 / actual buffer dimensions). Use this instead of
    // baseRenderer.TexelSize() for any in-buffer step (blur / denoise / bloom): half-res scratch buffers
    // have a different texel size than the full-res scene / viewport.
    inline TexCoord TexelSize(void) noexcept {
        return TexCoord(1.0f / float(GetWidth()), 1.0f / float(GetHeight()));
    }

    inline bool IsAvailable(void) noexcept {
        return m_isAvailable;
    }

    inline void SetClearColor(RGBAColor color) noexcept {
        m_clearColor = color;
    }

    inline Viewport& GetViewport(void) noexcept {
        return m_viewport;
    }

    inline GfxFilterMode Filtering(void) noexcept {
        return m_filtering;
    }

    // Hands the filter down to the render texture and makes it re-apply its parameters.
    void SetFiltering(GfxFilterMode filtering);

    inline bool IsEnabled(void) noexcept {
#if 1
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_activeHandle);
#endif
        return (GLuint(m_activeHandle) != GL_NONE) and (GLuint(m_activeHandle) == m_handle.Data());
    }

    inline int GetLastDestination(void) noexcept {
        return m_lastDestination;
    }

    inline void SetLastDestination(int i) noexcept {
        m_lastDestination = i;
    }

    inline int NextBuffer(int i) noexcept {
        return (i + 1) % m_bufferCount;
    }

    inline GLuint GetHandle(int bufferIndex) {
        return m_bufferInfo[bufferIndex].m_handle;
    }

    // The wrapper for one colour buffer. nullptr if there is no such buffer.
    RenderTargetTexture* GetRenderTexture(int bufferIndex = 0) noexcept;

    // attachment < 0: the buffer's own attachment point (m_attachment); otherwise the point given -
    // that is how dbSingle puts the selected colour buffer on GL_COLOR_ATTACHMENT0.
    bool AttachBuffer(int bufferIndex, int attachment = -1);

    bool DetachBuffer(int bufferIndex);

    bool BindBuffer(int bufferIndex, int tmuIndex = -1);

    void ReleaseBuffers(void);

    // Custom draw-buffer setup: bypasses the standard groups (dbAll / dbColor / dbExtra / dbSingle) so a
    // pass can bind an arbitrary set of this target's buffers, in an arbitrary slot order, without the
    // general draw-buffer handling interfering. Entry i of the list is the buffer index bound to fragment
    // output slot i, or CUSTOM_DRAW_BUFFER_NONE to leave that slot unwritten. Buffers not named in the
    // list are detached. Stays in effect until another draw-buffer group is selected; a Reactivate (or any
    // Activate with dbCustom) re-applies it. The depth buffer is unaffected and follows the usual rules.
    void SelectCustomDrawBuffers(const CustomDrawBufferList& bufferIndices);

    SharedTextureHandle& BufferHandle(int bufferIndex) {
#ifdef _DEBUG
        if (bufferIndex < m_bufferCount)
            return m_bufferInfo[bufferIndex].m_handle;
        return Texture::nullHandle;
#else
        return (bufferIndex < m_bufferCount) ? m_bufferInfo[bufferIndex].m_handle : Texture::nullHandle;
#endif
    }

    // Points the framebuffer at one face of a cube map buffer. The target must be active; everything
    // drawn afterwards lands on that face until another one is selected. Six calls with a draw in
    // between capture the whole surroundings of a point.
    bool SelectCubeFace(int face, int bufferIndex = -1);

    inline int CubeMapIndex(int i = 0) noexcept {
        return m_cubeMapCount ? m_cubeMapIndex + i : -1;
    }

    inline int CubeMapCount(void) noexcept {
        return m_cubeMapCount;
    }

    inline int ExtraBufferIndex(int i = 0) noexcept {
        return m_extraBufferCount ? m_extraBufferIndex + i : -1;
    }

    inline int VertexBufferIndex(int i = 0) noexcept {
        return ExtraBufferIndex(i);
    }


    inline int DepthBufferIndex(void) noexcept {
        return m_depthBufferIndex;
    }


    inline bool operator==(const RenderTarget& other) const noexcept {
        return m_handle == other.m_handle;
    }


    inline bool operator!=(const RenderTarget& other) const noexcept {
        return m_handle != other.m_handle;
    }

    inline DrawBufferList& DrawBuffers(void) noexcept {
        return m_drawBuffers;
    }

    inline void ClearColorBuffers(void) noexcept {
        return gfxStates.ClearColorBuffers();
    }

    // WBOIT accum/revealage per-buffer clear: clear a single draw buffer of the bound FBO (accum -> 0,
    // revealage -> 1). Call right after Activate (the FBO + its draw-buffer mapping must be bound).
    inline void ClearColorBuffer(int bufferIndex, RGBAColor color) {
        glClearBufferfv(GL_COLOR, bufferIndex, color.Data());
    }

    inline bool HaveDepthBuffer(bool checkHandle = true) noexcept {
        return (m_depthBufferIndex >= 0) and (not checkHandle or m_bufferInfo[m_depthBufferIndex].m_handle);
    }

    // The depth buffer carries a stencil plane (see m_stencilBufferIndex).
    inline bool HaveStencilBuffer(bool checkHandle = true) noexcept {
        return m_hasStencil and HaveDepthBuffer(checkHandle);
    }

    // Depth/stencil write state for the requested depth mode, identical in all three backends: dbmReadOnly
    // means "test against the depth buffer, never write it", which is what allows the same buffer to be
    // sampled while it stays bound. DX and Vulkan additionally reject a writing pipeline over a read-only
    // depth view, so turning the writes off here is not optional there either. Only the stencil plane of
    // the ACTIVE depth buffer (own, or a shared source's) is considered. dbmWrite deliberately restores
    // nothing: every stage sets the states it needs (render state contract).
    inline void SetDepthMode(eDepthBufferMode depthMode) {
        m_depthMode = depthMode;
        if (depthMode != dbmReadOnly)
            return;
        gfxStates.SetDepthWrite(0);
        RenderTarget* depthOwner = (m_depthSource != nullptr) ? m_depthSource : this;
        if (depthOwner->HaveStencilBuffer(true))
            gfxStates.SetStencilWrite(0);
    }

    inline void ClearDepthBuffer(float clearValue = 1.0f) noexcept {
        if (HaveDepthBuffer(true))
            gfxStates.ClearDepthBuffer(clearValue);
    }

    inline void ClearStencilBuffer(int clearValue = 0) noexcept {
        // Gated like ClearDepthBuffer: without a stencil plane the glClear would be a no-op that still
        // costs a state change, and on a target with no depth buffer at all it would clear the wrong FBO's.
        if (HaveStencilBuffer(true))
            gfxStates.ClearStencilBuffer(clearValue);
    }

private:
    void CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType, bool isMRT);

    int CreateSpecialBuffers(BufferInfo::eBufferType bufferType, int& attachmentIndex, int bufferCount);

    bool AttachBuffers(bool hasMRTs);

    void ApplyCustomDrawBuffers(void);

    bool ReattachBuffers();

    // Put every buffer that dbSingle moved to a foreign attachment point back off that point, so the
    // canonical layout can be restored without one buffer's detach tearing down another one's slot.
    void ReleaseRemappedBuffers(void);

    bool DepthBufferIsActive(int bufferIndex, eDrawBufferGroups drawBufferGroup);

    void CreateRenderArea(void);
};

// =================================================================================================
