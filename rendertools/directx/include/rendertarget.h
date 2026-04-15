#pragma once

#include "framework.h"
#include "array.hpp"
#include "viewport.h"
#include "texture.h"
#include "colordata.h"
#include "descriptor_heap.h"
#include "commandlist.h"

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

    String       m_name;
    int          m_width{ 0 };
    int          m_height{ 0 };
    int          m_scale{ 1 };
    int          m_bufferCount{ 0 };
    int          m_colorBufferCount{ 0 };
    int          m_activeBufferIndex{ 0 };
    int          m_lastDestination{ -1 };
    bool         m_pingPong{ false };
    bool         m_isAvailable{ false };
	bool         m_haveRTVs{ false };
    eDrawBufferGroups m_drawBufferGroup{ dbAll };

    Viewport     m_viewport;
    Viewport*    m_viewportSave{ nullptr };
    RenderTargetTexture   m_renderTexture;   // lightweight proxy used for RenderTarget→quad rendering
    RenderTargetTexture   m_depthTexture;

    // DX12 resources (one entry per color buffer slot)
    ComPtr<ID3D12Resource>  m_colorResources[RT_MAX_COLOR_BUFFERS];
    DescriptorHandle        m_rtvHandles[RT_MAX_COLOR_BUFFERS];
    DescriptorHandle        m_srvHandles[RT_MAX_COLOR_BUFFERS];
    uint32_t                m_srvIndices[RT_MAX_COLOR_BUFFERS];  // mirrors m_srvHandles[i].index

    // Depth/stencil
    ComPtr<ID3D12Resource>  m_depthResource;
    DescriptorHandle        m_dsvHandle;

    // Current resource state for each color buffer (needed for barriers)
    D3D12_RESOURCE_STATES   m_colorStates[RT_MAX_COLOR_BUFFERS]{};

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

    bool AllocRTV(ID3D12Device* device, D3D12_RENDER_TARGET_VIEW_DESC& rtvd, int i);

    bool AllocRTVs(void);

    void FreeRTVs(void);

    void FreeSRVs(void);
    
    void SetName(const String& name) noexcept {
		m_name = name;
	}

    // Enable / disable — set this RenderTarget's RTVs as the active render target.
    bool Enable(int bufferIndex = -1, eDrawBufferGroups drawBufferGroup = dbAll, bool clear = true, bool reenable = false);

    bool EnableBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable);

    inline bool Reenable(bool clear = false, bool reenable = false) {
        return Enable(m_activeBufferIndex, m_drawBufferGroup, clear, reenable);
    }

    void Disable(bool flush = false);

    // Called by DrawBufferHandler::SetActiveDrawBuffers().
    void BindRenderTargets(ID3D12GraphicsCommandList* list);

    inline CommandList* GetCmdList(void) noexcept { return m_cmdList; }

    void SetViewport(bool flipVertically = false) noexcept;

    void Fill(RGBAColor color);

    void Clear(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear);

    void ClearStencil(void);

    // Render helpers (same as OGL)
    Texture* GetRenderTexture(const RTRenderParams& params, int tmuIndex = 0);

    Texture* GetDepthTexture(void);
    
    bool UpdateTransformation(const RTRenderParams& params);
    
    bool RenderTexture(Texture* texture, const RTRenderParams& params, const RGBAColor& color);
    
    inline bool RenderTexture(Texture* tex, const RTRenderParams& p, RGBAColor&& c) { 
        return RenderTexture(tex, p, static_cast<const RGBAColor&>(c)); 
    }
    
    inline bool RenderTexture(Texture* tex, const RTRenderParams& p) { 
        return RenderTexture(tex, p, ColorData::White); 
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
        return true; 
    }

    // Returns the SRV index for the given color buffer — used by base_renderer.cpp:
    //   m_renderTexture.m_handle = renderTarget->BufferHandle(0);
    uint32_t& BufferHandle(int bufferIndex);

    inline bool operator==(const RenderTarget& o) const noexcept { 
        return this == &o; 
    }
    
    inline bool operator!=(const RenderTarget& o) const noexcept { 
        return this != &o;
    }

    // Stubs kept for OGL-compat callers
    inline bool AttachBuffer(int) { 
        return true; 
    }
    
    inline bool DetachBuffer(int) { 
        return true; 
    }
    
    inline void ReleaseBuffers() {}
    
    inline int  DepthBufferIndex() noexcept {
        return m_colorBufferCount; 
    }

#pragma warning(push)
#pragma warning(disable:4100)  // unreferenced formal parameter (VertexBufferIndex)
    inline int  VertexBufferIndex(int i = 0) noexcept { 
        return 0; 
    }
#pragma warning(pop)

private:

    bool CreateColorBuffer(int i, int width, int height);

    bool CreateDepthBuffer(int width, int height);

    void TransitionColor(ID3D12GraphicsCommandList* list, int i, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    void CreateRenderArea(void);
};

// =================================================================================================
