#pragma once

#include "framework.h"
#include "array.hpp"
#include "viewport.h"
#include "texture.h"
#include "colordata.h"
#include "descriptor_heap.h"
#include "commandlist.h"
#include "base_quad.h"

// =================================================================================================
// DX12 RenderTarget (Frame Buffer Object)
//
// In OpenGL an RenderTarget bound a set of texture attachments as render targets.  In DX12 render targets
// are set with OMSetRenderTargets and must be backed by RTV descriptors.
//
// This class manages:
//   • Up to RT_MAX_COLOR_BUFFERS color render targets (default-heap Texture2D resources + RTV).
//   • One depth/stencil target (default-heap Texture2D with D32_FLOAT format + DSV).
//   • SRV descriptors for each color buffer so they can be sampled in subsequent passes.
//   • BufferHandle(i) returns a uint32_t& (SRV index) compatible with Texture::m_handle.
//   • BindRenderTargets(list) → OMSetRenderTargets, called by DrawBufferHandler.
//   • Ping-pong: destination >= 0 issues a Resource Barrier SRV→RTV and back after render.
//   • destination == -1: bind current RenderTarget's RTVs without resource barriers.
//
// RTBufferParams and RTRenderParams are kept identical to the OGL version for source compat.

static constexpr int RT_MAX_COLOR_BUFFERS = 4;

#define INVALID_BUFFER_INDEX 0x80000000

// =================================================================================================

class BufferInfo {
public:
    typedef enum {
        btColor,
        btDepth,
        btStencil,
        btVertex
    } eBufferType;

    ComPtr<ID3D12Resource>  m_resource;
    DescriptorHandle        m_rtvHandle;
    DescriptorHandle        m_srvHandle;
    DescriptorHandle        m_dsvHandle;
    uint32_t                m_srvIndex{ UINT32_MAX };
    D3D12_RESOURCE_STATES   m_state{ D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    eBufferType             m_type{ btColor };

    void Init(void);

    void SetState(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES targetState);

    bool AllocRTV(void);

    void Release(void);
};

// =================================================================================================

class RenderTarget
{
public:
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

    struct RTBufferParams {
        String name{ "" };
        int colorBufferCount{ 1 };
        int depthBufferCount{ 0 };
        int stencilBufferCount{ 0 };
        int vertexBufferCount{ 0 };
        bool hasMRTs{ false };
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

    // -------------------------------------------------------------------------

    String      m_name;
    int         m_width{ 0 };
    int         m_height{ 0 };
    int         m_scale{ 1 };
    int         m_bufferCount{ 0 };
    int         m_colorBufferCount{ 0 };
    int         m_vertexBufferCount{ 0 };
    int         m_vertexBufferIndex{ -1 };
    int         m_depthBufferIndex{ -1 };
    int         m_stencilBufferIndex{ -1 };
    int         m_activeBufferIndex{ 0 };
    int         m_lastDestination{ -1 };
    bool        m_pingPong{ false };
    bool        m_isAvailable{ false };
    bool        m_haveRTVs{ false };
	RGBAColor   m_clearColor{ ColorData::Invisible };
    eDrawBufferGroups m_drawBufferGroup{ dbAll };

    Viewport     m_viewport;
    Viewport*    m_viewportSave{ nullptr };
    RenderTargetTexture     m_renderTexture;
    RenderTargetTexture     m_depthTexture;
    BaseQuad                m_viewportArea;

    AutoArray<BufferInfo>   m_bufferInfo;

    // Own command list — all rendering into this RenderTarget is recorded here.
    CommandList*            m_cmdList{ nullptr };

    // -------------------------------------------------------------------------

    RenderTarget();

    ~RenderTarget() { 
        Destroy(); 
    }

    void Init(void);

    bool Create(int width, int height, int scale, const RTBufferParams& params);

    void Destroy(void);

    bool AllocRTVs(void);

    void FreeRTVs(void);
    
    void SetName(const String& name) noexcept {
		m_name = name;
	}

    bool Enable(int bufferIndex = -1, eDrawBufferGroups drawBufferGroup = dbAll, bool clear = true, bool reenable = false);

    bool EnableBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable);

    bool SelectDrawBuffers(int bufferIndex = -1, eDrawBufferGroups drawBufferGroup = dbAll);

    bool SetDrawBuffers(int bufferIndex = -1, eDrawBufferGroups drawBufferGroup = dbAll, bool reenable = false);

    bool DepthBufferIsActive(int bufferIndex, eDrawBufferGroups drawBufferGroup);

    inline bool Reenable(bool clear = false, bool reenable = false) {
        return Enable(m_activeBufferIndex, m_drawBufferGroup, clear, reenable);
    }

    void Disable(bool flush = false, bool restoreDrawBuffer = true);

    inline CommandList* GetCmdList(void) noexcept { return m_cmdList; }

    void SetViewport(bool flipVertically = false) noexcept;

    inline void SetClearColor(RGBAColor color) noexcept {
        m_clearColor = color;
    }

    void Fill(RGBAColor color);

    void Clear(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear);

    void ClearStencil(void);

    // Render helpers (same as OGL)
    Texture* GetRenderTexture(const RTRenderParams& params, int tmuIndex = 0);

    Texture* GetDepthTexture(void);
    
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
    
    inline RenderTargetTexture* GetTexture(void) noexcept { 
        return &m_renderTexture; 
    }

    // In DX12 there is no explicit framebuffer binding state — always report enabled.
    inline bool IsEnabled(void)  noexcept { 
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

    inline void ReleaseBuffers() {}

    inline int DepthBufferIndex(void) noexcept {
        return m_depthBufferIndex;
    }

    inline int VertexBufferIndex(int i = 0) noexcept {
        return m_vertexBufferIndex + i;
    }

private:

    void CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType);

    int CreateSpecialBuffers(BufferInfo::eBufferType bufferType, int& attachmentIndex, int bufferCount);

    void CreateRenderArea(void);
};

// =================================================================================================
