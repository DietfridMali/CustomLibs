#pragma once

#include "vkframework.h"
#include "image_layout_tracker.h"
#include "array.hpp"
#include "viewport.h"
#include "texture.h"
#include "colordata.h"
#include "commandlist.h"
#include "base_quad.h"
#include "drawbufferhandler.h"

// =================================================================================================
// Vulkan RenderTarget (Frame Buffer Object)
//
// In OpenGL a RenderTarget bound a set of texture attachments as render targets. In DX12 render
// targets are set with OMSetRenderTargets and need RTV descriptors. In Vulkan 1.3 with
// dynamic_rendering (Core in 1.3) we describe the attachments via VkRenderingAttachmentInfo
// and call vkCmdBeginRendering / vkCmdEndRendering on the command buffer — no preallocated
// RTV/DSV descriptor objects required.
//
// This class manages:
//   • Up to RT_MAX_COLOR_BUFFERS color attachments (VkImage + VmaAllocation + VkImageView).
//   • One depth/stencil attachment (VkImage with VK_FORMAT_D24_UNORM_S8_UINT + depth ImageView).
//   • An image view per attachment used both as render-target attachment (color/depth) and
//     as shader-read source (combined image sampler bind in subsequent passes).
//   • BufferHandle(i) returns a uint32_t& (logical id, kept for source compatibility with
//     m_renderTexture.m_handle assignments).
//   • Image-layout transitions through ImageLayoutTracker (per-buffer member).
//   • Ping-pong: destination >= 0 issues a SHADER_READ → COLOR_ATTACHMENT transition before
//     and back after render.
//
// RTCreationParams and RTRenderParams are kept identical to the OGL/DX12 version.

static constexpr int RT_MAX_COLOR_BUFFERS = 4;

#define INVALID_BUFFER_INDEX 0x80000000

// =================================================================================================

class BufferInfo {
public:
    typedef enum {
        btColor,
        btDepth,
        btStencil,
        btVertex,
        btSkyMap   // R16G16B16A16_SFLOAT, color+sampled+storage usage — compute-only target
    } eBufferType;

    VkImage             m_image       { VK_NULL_HANDLE };
    VmaAllocation       m_allocation  { VK_NULL_HANDLE };
    VkImageView         m_imageView   { VK_NULL_HANDLE };  // attachment view + sampling source
    VkImageView         m_depthSampleView { VK_NULL_HANDLE };  // depth-only sampling view (set for btDepth)
    ImageLayoutTracker  m_layoutTracker;
    uint32_t            m_srvIndex    { UINT32_MAX };  // logical id for source-compat
    eBufferType         m_type        { btColor };
    VkFormat            m_colorFormat { VK_FORMAT_R8G8B8A8_UNORM };  // per-RT color format; HDR scene/sky use R16G16B16A16_SFLOAT

    void Init(void);

    // Replaces the DX12 SetState(cmdList, D3D12_RESOURCE_STATES). Maps a coarse "what is this for"
    // hint to the right Vulkan layout/stage/access via the tracker.
    void SetState(VkCommandBuffer cb, eBufferType usageHint, bool asShaderRead);

    void Release(void);
};

// =================================================================================================

class RenderTarget
{
public:
    using DrawBufferList = DrawBufferHandler::DrawBufferList; // required for high level compatibility

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

    // Shared API with the DX backend (read-only/sampleable depth for soft particles etc.). Not yet acted
    // on here -- Vulkan keeps the normal writable depth until the feature is ported.
    typedef enum {
        dbmWrite,
        dbmReadOnly
    } eDepthBufferMode;

    struct RTCreationParams {
        String name{ "" };
        int colorBufferCount{ 1 };
        VkFormat colorFormat{ VK_FORMAT_R8G8B8A8_UNORM };  // R16G16B16A16_SFLOAT for HDR color targets
        int depthBufferCount{ 0 };
        int stencilBufferCount{ 0 };
        int vertexBufferCount{ 0 };
        // Compute-only storage textures (R16G16B16A16_SFLOAT, COLOR+SAMPLED+STORAGE usage).
        // Occupy m_bufferInfo[m_computeBufferIndex..] — caller addresses them via that offset.
        int skyMapCount{ 0 };
        bool hasMRTs{ false };
        bool isScreenBuffer{ false };
        bool storageImage{ false };  // legacy; superseded by skyMapCount
    };

    struct RTRenderParams {
        int    source{ 0 };
        int    destination{ -1 };
        bool   clearBuffer{ true };
        bool   premultiply{ false };
        int    flipVertically{ 0 };
        bool   centerOrigin{ true };
        float  rotation{ 0.0f };
        float  scale{ 1.0f };
        Shader* shader{ nullptr };
    };

	struct RTActivationParams {
		int bufferIndex{ -1 };
		eDrawBufferGroups drawBufferGroup{ dbAll };
        bool clear{ true };
		bool reactivate{ false };
		eDepthBufferMode depthMode{ dbmWrite };
	};

    // -------------------------------------------------------------------------

    String              m_name;
    int                 m_width{ 0 };
    int                 m_height{ 0 };
    int                 m_scale{ 1 };
    int                 m_bufferCount{ 0 };
    int                 m_colorBufferCount{ 0 };
    VkFormat            m_colorFormat{ VK_FORMAT_R8G8B8A8_UNORM };
    int                 m_vertexBufferCount{ 0 };
    int                 m_extraBufferIndex{ -1 };
    int                 m_depthBufferIndex{ -1 };
    int                 m_stencilBufferIndex{ -1 };
    int                 m_computeBufferIndex{ -1 };   // start of compute-buffer slot range in m_bufferInfo
    int                 m_computeBufferCount{ 0 };
    int                 m_activeBufferIndex{ 0 };
    int                 m_lastDestination{ -1 };
    bool                m_pingPong{ false };
    bool                m_isAvailable{ false };
    bool                m_isScreenBuffer{ false };
    bool                m_isInRendering{ false };  // active vkCmdBeginRendering scope
    bool                m_wasActivated{ false };
    RGBAColor           m_clearColor{ ColorData::Invisible };
    eDrawBufferGroups   m_drawBufferGroup{ dbAll };
    DrawBufferList      m_drawBuffers{};

    RenderStates        m_renderStates{};
    Viewport            m_viewport;
    RenderTarget*       m_depthSource{ nullptr };   // foreign depth buffer to bind/test against instead of an own one (SetDepthSource)
    Viewport*           m_viewportSave{ nullptr };
    RenderTargetTexture m_renderTexture;
    RenderTargetTexture m_depthTexture;
    ShadowTexture       m_shadowTexture; // ShadowTexture mit Compare-Sampler fuer HW-PCF (sampler2DShadow-Aequivalent)
    BaseQuad            m_viewportArea;

    AutoArray<BufferInfo>   m_bufferInfo;

    // Own command list — rendering recorded for this RT goes through it.
    CommandList*        m_cmdList{ nullptr };

    // -------------------------------------------------------------------------

    RenderTarget();

    ~RenderTarget() {
        Destroy();
    }

    void Init(void);

    bool Create(int width, int height, int scale, const RTCreationParams& params);

    void Destroy(void);

    void SetName(const String& name) noexcept {
        m_name = name;
        if (m_cmdList)
            m_cmdList->SetName(name);
    }

    bool Activate(const RTActivationParams& params);

    bool IsActive(void) noexcept;

    bool EnableBuffers(const RTActivationParams& params);

    bool SelectDrawBuffers(const RTActivationParams& params);

    inline bool Reactivate(bool clear = false) noexcept {
        RTActivationParams params{ .bufferIndex = m_activeBufferIndex, .drawBufferGroup = m_drawBufferGroup, .clear = clear, .reactivate = true };
        return Activate(params);
    }

    void Deactivate(void) noexcept;

    bool Enable(const RTActivationParams& params);

    void Disable(bool deactivate = true) noexcept;

    bool DepthBufferIsActive(int bufferIndex, eDrawBufferGroups drawBufferGroup);

    inline void Flush(void) noexcept {
        if (m_cmdList)
            m_cmdList->Flush();
    }

    inline CommandList* GetCmdList(void) noexcept { return m_cmdList; }

    void SetViewport(bool flipVertically = false) noexcept;

    inline void SetClearColor(RGBAColor color) noexcept {
        m_clearColor = color;
    }

    void Fill(RGBAColor color);

    void Clear(const RTActivationParams& params);

    void ClearColorBuffers(void);

    // WBOIT accum/revealage per-buffer clear: clear a single colour attachment of the bound FBO to its
    // own value (accum -> 0, revealage -> 1). Call right after Activate (inside the render scope).
    void ClearColorBuffer(int bufferIndex, RGBAColor color);

    void ClearDepthBuffer(float clearValue = 1.0f);

    void ClearStencilBuffer(void);

    // Share another render target's depth buffer: while set, activating this target binds the
    // source's depth image view as the depth attachment instead of an own one (this target needs
    // none of its own). The foreign depth is never cleared (loadOp is forced to LOAD even when the
    // activation clears) — all Clear*/GetDepth* paths stay own-buffer-based — and is meant to be
    // tested against, not written (leave depth write off). Lets an overlay pass (e.g. the wet-splat
    // decal buffer) hardware-depth-test against the scene. Pass nullptr to unshare.
    inline void SetDepthSource(RenderTarget* source) noexcept {
        m_depthSource = source;
    }

    // Render helpers (same as OGL)
    Texture* GetAsTexture(const RTRenderParams& params, int tmuIndex = 0);

    Texture* GetDepthAsTexture(void);

    Texture* GetDepthAsShadowTexture(void);

    bool UpdateTransformation(const RTRenderParams& params);

    bool RenderAsTexture(Texture* texture, const RTRenderParams& params, const RGBAColor& color);

    inline bool RenderAsTexture(Texture* tex, const RTRenderParams& p, RGBAColor&& c) {
        return RenderAsTexture(tex, p, static_cast<const RGBAColor&>(c));
    }

    inline bool RenderAsTexture(Texture* tex, const RTRenderParams& p) {
        return RenderAsTexture(tex, p, ColorData::White);
    }

    bool Render(const RTRenderParams& params, const RGBAColor& color);

    inline bool Render(const RTRenderParams& p, RGBAColor&& c) {
        return Render(p, static_cast<const RGBAColor&>(c));
    }

    inline bool  Render(const RTRenderParams& p) {
        return Render(p, ColorData::White);
    }

    bool AutoRender(const RTRenderParams& params, const RGBAColor& color);

    inline bool  AutoRender(const RTRenderParams& p, RGBAColor&& c) {
        return AutoRender(p, static_cast<const RGBAColor&>(c));
    }

    inline bool  AutoRender(const RTRenderParams& p) {
        return AutoRender(p, ColorData::White);
    }

    inline int  GetWidth(bool scaled = false) noexcept {
        return scaled ? m_width * m_scale : m_width;
    }

    inline int GetHeight(bool scaled = false) noexcept {
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

    inline Viewport& GetViewport(void) noexcept {
        return m_viewport;
    }

    inline int  GetLastDestination(void) noexcept {
        return m_lastDestination;
    }

    inline void SetLastDestination(int i) noexcept {
        m_lastDestination = i;
    }

    inline int  NextBuffer(int i) noexcept {
        return (i + 1) % m_bufferCount;
    }

    inline RenderTargetTexture* GetRenderTexture(void) noexcept {
        return &m_renderTexture;
    }

    inline bool IsEnabled(void) noexcept {
        return m_cmdList and m_cmdList->IsRecording();
    }

    uint32_t& BufferHandle(int bufferIndex);

    inline bool operator==(const RenderTarget& o) const noexcept {
        return this == &o;
    }

    inline bool operator!=(const RenderTarget& o) const noexcept {
        return this != &o;
    }

    bool AttachBuffer(int bufferIndex);

    bool DetachBuffer(int bufferIndex);

    bool BindBuffer(int bufferIndex, int tmuIndex = -1);

    inline void ReleaseBuffers() {}

    inline int DepthBufferIndex(void) noexcept {
        return m_depthBufferIndex;
    }

    inline int ExtraBufferIndex(int i = 0) noexcept {
        return m_extraBufferIndex + i;
    }

    inline int VertexBufferIndex(int i = 0) noexcept {
        return ExtraBufferIndex(i);
    }

    inline DrawBufferList& DrawBuffers(void) noexcept {
        return m_drawBuffers;
    }

    // Populates a PipelineKey with the colour/depth attachment formats currently active for
    // this RT (selected by m_drawBufferGroup + m_activeBufferIndex). Used by CommandList::
    // GetPipeline to feed pipelineCache.GetOrCreate. Caller fills key.shader / key.states.
    void FillPipelineKey(struct PipelineKey& key) noexcept;

    // Manage the active vkCmdBeginRendering scope for this RT. Parameterless call (defaults
    // false/false) gives LOAD_OP_LOAD on both — used to resume a previously suspended scope.
    void BeginRendering(bool clearColor = false, bool clearDepth = false);
    void EndRendering(void);

private:
    void CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType);

    bool CreateSRV(BufferInfo& info, VkFormat viewFormat, VkImageAspectFlags aspect);

    void CreateColorBuffer(BufferInfo& info, int w, int h);

    void CreateDepthBuffer(BufferInfo& info, int w, int h);

    int CreateSpecialBuffers(BufferInfo::eBufferType bufferType, int& attachmentIndex, int bufferCount);

    void CreateRenderArea(void);

    inline bool HaveDepthBuffer(bool checkHandle = true) noexcept {
        return (m_depthBufferIndex >= 0)
            and (not checkHandle or (m_bufferInfo[m_depthBufferIndex].m_imageView != VK_NULL_HANDLE));
    }

    // A shared depth source (SetDepthSource) takes precedence over an own depth buffer for the
    // bound depth attachment / pipeline depth format. The Clear*/GetDepth* paths intentionally keep
    // using the own buffer, so the foreign depth is never cleared or written.
    inline bool HaveActiveDepthBuffer(void) noexcept {
        return HaveDepthBuffer(true) or ((m_depthSource != nullptr) and m_depthSource->HaveDepthBuffer(true));
    }

    inline BufferInfo* ActiveDepthInfo(void) noexcept {
        if (m_depthSource != nullptr)
            return m_depthSource->HaveDepthBuffer(true) ? &m_depthSource->m_bufferInfo[m_depthSource->m_depthBufferIndex] : nullptr;
        return HaveDepthBuffer(true) ? &m_bufferInfo[m_depthBufferIndex] : nullptr;
    }

    // DepthBufferHandle in DX12 returned a CPU descriptor handle pointer. In Vulkan there is no
    // such thing — depth attachment is described inline in the VkRenderingAttachmentInfo built
    // from m_bufferInfo[m_depthBufferIndex].m_imageView. The accessor is removed; callers
    // consult m_bufferInfo[m_depthBufferIndex] directly.
};

// =================================================================================================
