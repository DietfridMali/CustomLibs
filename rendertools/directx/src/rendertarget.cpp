#define NOMINMAX

#include "rendertarget.h"
#include "base_renderer.h"
#include "base_shaderhandler.h"
#include "commandlist.h"
#include "dx12context.h"
#include "gfxdriverstates.h"

// =================================================================================================
// DX12 RenderTarget implementation

static constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

// -------------------------------------------------------------------------------------------------

static ComPtr<ID3D12Resource> CreateRTResource(ID3D12Device* device,
                                                int w, int h,
                                                DXGI_FORMAT fmt,
                                                D3D12_RESOURCE_STATES initState,
                                                const D3D12_CLEAR_VALUE* clearVal) noexcept
{
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension  = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width      = UINT(w);
    rd.Height     = UINT(h);
    rd.DepthOrArraySize = 1;
    rd.MipLevels  = 1;
    rd.Format     = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout     = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags      = (fmt == kDepthFormat)
                        ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                        : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
                        | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ComPtr<ID3D12Resource> res;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        initState, clearVal, IID_PPV_ARGS(&res));
    return res;
}

// =================================================================================================

RenderTarget::RenderTarget()
{
    Init();
}


void RenderTarget::Init(void)
{
    m_width = m_height = 0;
    m_scale = 1;
    m_bufferCount = m_colorBufferCount = 0;
    m_activeBufferIndex = 0;
    m_lastDestination = -1;
    m_pingPong = false;
    m_isAvailable = false;
    m_drawBufferGroup = dbAll;
    for (int i = 0; i < RT_MAX_COLOR_BUFFERS; ++i) {
        m_colorResources[i].Reset();
        m_rtvHandles[i] = {};
        m_srvHandles[i] = {};
        m_srvIndices[i] = UINT32_MAX;
        m_colorStates[i] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    m_depthResource.Reset();
    m_dsvHandle = {};
}


bool RenderTarget::CreateColorBuffer(int i, int width, int height)
{
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    D3D12_CLEAR_VALUE cv{};
    cv.Format = kColorFormat;

    m_colorResources[i] = CreateRTResource(device, width, height, kColorFormat, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv);
    if (not m_colorResources[i])
        return false;
    m_colorStates[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // RTV
    D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
    rtvd.Format  = kColorFormat;
    rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    if (not AllocRTV(device, rtvd, i))
        return false;

    // SRV
    m_srvHandles[i] = descriptorHeaps.AllocSRV();
    if (not m_srvHandles[i].IsValid())
        return false;
    m_srvIndices[i] = m_srvHandles[i].index;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = kColorFormat;
    srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_colorResources[i].Get(), &srvd, m_srvHandles[i].cpu);

    // Transition RT → PSR using the RenderTarget's own list (already open from Create()).
    // m_colorStates[i] accurately reflects the actual GPU state because the list IS open.
    auto* list = m_cmdList.List();
    if (list) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = m_colorResources[i].Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        m_colorStates[i] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    return true;
}


bool RenderTarget::CreateDepthBuffer(int width, int height)
{
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    D3D12_CLEAR_VALUE cv{};
    cv.Format = kDepthFormat;
    cv.DepthStencil.Depth = 1.0f;
    cv.DepthStencil.Stencil = 0;

    m_depthResource = CreateRTResource(device, width, height, kDepthFormat, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv);
    if (not m_depthResource)
        return false;

    m_dsvHandle = descriptorHeaps.AllocDSV();
    if (not m_dsvHandle.IsValid())
        return false;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format = kDepthFormat;
    dsvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dx12Context.Device()->CreateDepthStencilView(m_depthResource.Get(), &dsvd, m_dsvHandle.cpu);
    return true;
}


bool RenderTarget::Create(int width, int height, int scale, const RTBufferParams& params)
{
    Destroy();

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;

    m_name = params.name;
    m_width = width;
    m_height = height;
    m_scale = scale;
    m_colorBufferCount = std::min(params.colorBufferCount, RT_MAX_COLOR_BUFFERS);
    m_bufferCount = m_colorBufferCount;
    m_pingPong = m_bufferCount > 1;

    int w = width * scale;
    int h = height * scale;

    // Create the RenderTarget's own command list and open it so CreateColorBuffer can record barriers.
    if (not m_cmdList.Create(device, m_name.IsEmpty() ? String("RenderTarget") : m_name))
        return false;
    if (not m_cmdList.Open(commandListHandler.CmdQueue().FrameIndex()))
        return false;

    for (int i = 0; i < m_colorBufferCount; ++i) {
        if (not CreateColorBuffer(i, w, h)) {
            Destroy();
            return false;
        }
    }
    m_haveRTVs = true;

    if (params.depthBufferCount > 0 /* or (params.colorBufferCount > 0) */) {
        if (not CreateDepthBuffer(w, h)) {
            Destroy();
            return false;
        }
    }

    // Submit the initial RT→PSR barriers immediately and wait; leaves the list closed.
    m_cmdList.Flush();

    m_viewport = Viewport(0, 0, w, h);
    m_isAvailable = true;
    return true;
}


bool RenderTarget::AllocRTV(ID3D12Device* device, D3D12_RENDER_TARGET_VIEW_DESC& rtvd, int i) {
    m_rtvHandles[i] = descriptorHeaps.AllocRTV();
    if (not m_rtvHandles[i].IsValid())
        return false;
    device->CreateRenderTargetView(m_colorResources[i].Get(), &rtvd, m_rtvHandles[i].cpu);
    return true;
}


bool RenderTarget::AllocRTVs(void) {
	if (m_haveRTVs)
		return true;
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;
    D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
    rtvd.Format = kColorFormat;
    rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    for (int i = 0; i < m_colorBufferCount; ++i)
        if (not AllocRTV(device, rtvd, i))
            return false;
    return m_haveRTVs = true;
}


void RenderTarget::FreeRTVs(void) {
    for (int i = 0; i < m_colorBufferCount; ++i) {
        descriptorHeaps.FreeRTV(m_rtvHandles[i]);
        m_rtvHandles[i] = {};
    }
	m_haveRTVs = false;
}


void RenderTarget::FreeSRVs(void) {
    for (int i = 0; i < m_colorBufferCount; ++i) {
        descriptorHeaps.FreeSRV(m_srvHandles[i]);
        m_srvHandles[i] = {};
        m_srvIndices[i] = UINT32_MAX;
    }
}


void RenderTarget::Destroy(void)
{
    m_cmdList.Destroy();
    FreeRTVs();
    FreeSRVs();
    for (int i = 0; i < m_colorBufferCount; ++i) {
        m_colorResources[i].Reset();
        m_colorStates[i] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    descriptorHeaps.FreeDSV(m_dsvHandle);
    m_depthResource.Reset();
    m_dsvHandle = {};
    m_isAvailable = false;
    m_bufferCount = m_colorBufferCount = 0;
}


void RenderTarget::TransitionColor(ID3D12GraphicsCommandList* list, int i,
                           D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if ((before == after) or not list or not m_colorResources[i])
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = m_colorResources[i].Get();
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &b);
    m_colorStates[i] = after;
}


void RenderTarget::BindRenderTargets(ID3D12GraphicsCommandList* list)
{
    if (not list or not m_isAvailable)
        return;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[RT_MAX_COLOR_BUFFERS]{};
    int count = 0;
    for (int i = 0; i < m_colorBufferCount; ++i) {
        if (not m_colorResources[i])
            continue;
        TransitionColor(list, i, m_colorStates[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
        rtvs[count++] = m_rtvHandles[i].cpu;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = m_dsvHandle.IsValid() ? &m_dsvHandle.cpu : nullptr;
    list->OMSetRenderTargets(count, rtvs, FALSE, pDSV);

    D3D12_VIEWPORT vp{};
    vp.Width    = float(m_width * m_scale);
    vp.Height   = float(m_height * m_scale);
    vp.MaxDepth = 1.0f;
    list->RSSetViewports(1, &vp);
    D3D12_RECT sc{ 0, 0, m_width * m_scale, m_height * m_scale };
    list->RSSetScissorRects(1, &sc);
}


bool RenderTarget::Enable(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable)
{
    if (not m_isAvailable)
        return false;
	if (not AllocRTVs())
		return false;
    m_activeBufferIndex = (bufferIndex < 0) ? 0 : (bufferIndex % m_bufferCount);
    m_drawBufferGroup = drawBufferGroup;

    if (not m_cmdList.Open(commandListHandler.CmdQueue().FrameIndex()))
        return false;
    auto* list = m_cmdList.List();

    BindRenderTargets(list);

    if (clear) {
        const float zero[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < m_colorBufferCount; ++i)
            if (m_rtvHandles[i].IsValid())
                list->ClearRenderTargetView(m_rtvHandles[i].cpu, zero, 0, nullptr);
        if (m_dsvHandle.IsValid())
            list->ClearDepthStencilView(m_dsvHandle.cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
    return true;
}


bool RenderTarget::EnableBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable)
{
    return Enable(bufferIndex, drawBufferGroup, clear, reenable);
}


void RenderTarget::Disable(bool flush)
{
    // Transition all color buffers back to PSR so they can be sampled in subsequent passes.
    auto* list = m_cmdList.List();
    for (int i = 0; i < m_colorBufferCount; ++i)
        TransitionColor(list, i, m_colorStates[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (flush) {
        m_cmdList.Flush();
        FreeRTVs();
    }
    else
        m_cmdList.Close();
}


uint32_t& RenderTarget::BufferHandle(int bufferIndex)
{
    if (bufferIndex >= 0 and bufferIndex < m_colorBufferCount)
        return m_srvIndices[bufferIndex];
    static uint32_t invalid = UINT32_MAX;
    return invalid;
}


void RenderTarget::SetViewport(bool flipVertically) noexcept
{
    baseRenderer.SetViewport(m_viewport, 0, 0, flipVertically);
}


void RenderTarget::Fill(RGBAColor color)
{
    auto* list = m_cmdList.List();
    if (not list)
        return;
    float c[4] = { color.R(), color.G(), color.B(), color.A() };
    for (int i = 0; i < m_colorBufferCount; ++i)
        if (m_rtvHandles[i].IsValid())
            list->ClearRenderTargetView(m_rtvHandles[i].cpu, c, 0, nullptr);
}


void RenderTarget::Clear(int bufferIndex, eDrawBufferGroups /*drawBufferGroup*/, bool clear)
{
    if (not clear)
        return;
    auto* list = m_cmdList.List();
    if (not list)
        return;
    const float zero[4] = { 0, 0, 0, 0 };
    if (bufferIndex < 0) {
        for (int i = 0; i < m_colorBufferCount; ++i)
            if (m_rtvHandles[i].IsValid())
                list->ClearRenderTargetView(m_rtvHandles[i].cpu, zero, 0, nullptr);
    } else if ((bufferIndex < m_colorBufferCount) and m_rtvHandles[bufferIndex].IsValid()) {
        list->ClearRenderTargetView(m_rtvHandles[bufferIndex].cpu, zero, 0, nullptr);
    }
    if (m_dsvHandle.IsValid())
        list->ClearDepthStencilView(m_dsvHandle.cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}


Texture* RenderTarget::GetRenderTexture(const RTRenderParams& params, int /*tmuIndex*/)
{
    int src = params.source % m_bufferCount;
    if (not m_colorResources[src])
        return nullptr;
    m_renderTexture.m_handle = m_srvIndices[src];
    m_renderTexture.m_resource = m_colorResources[src];
    m_renderTexture.m_isValid  = true;
    return &m_renderTexture;
}


void RenderTarget::ClearStencil(void)
{
    if (not m_dsvHandle.IsValid())
        return;
    auto* list = m_cmdList.List();
    if (not list)
        return;
    list->ClearDepthStencilView(m_dsvHandle.cpu, D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
}


Texture* RenderTarget::GetDepthTexture(void)
{
    return nullptr;
}


bool RenderTarget::UpdateTransformation(const RTRenderParams& params)
{
    int dst = params.destination;
    if (dst < 0)
        return false;
    dst = dst % m_bufferCount;
    m_lastDestination = dst;

    baseRenderer.PushMatrix();
    baseRenderer.Translate(0.5f, 0.5f, 0.0f);
    float scale = params.scale;
    baseRenderer.Scale(scale, scale, 1.0f);
    if (params.rotation != 0.0f)
        baseRenderer.Rotate(params.rotation, 0.0f, 0.0f, 1.0f);
    return true;
}


bool RenderTarget::RenderTexture(Texture* texture, const RTRenderParams& params, const RGBAColor& color)
{
    if (not texture)
        return false;
    bool transformed = UpdateTransformation(params);
    baseRenderer.RenderToViewport(texture, color, params.rotation != 0.0f, false);
    if (transformed)
        baseRenderer.PopMatrix();
    return true;
}


bool RenderTarget::Render(const RTRenderParams& params, const RGBAColor& color)
{
    Texture* tex = GetRenderTexture(params);
    if (not tex)
        return false;
    int dst = params.destination;
    if (dst >= 0) {
        dst = dst % m_bufferCount;
        auto* list = m_cmdList.List();
        if (list) {
            TransitionColor(list, dst, m_colorStates[dst], D3D12_RESOURCE_STATE_RENDER_TARGET);
            list->OMSetRenderTargets(1, &m_rtvHandles[dst].cpu, FALSE,
                m_dsvHandle.IsValid() ? &m_dsvHandle.cpu : nullptr);
        }
        if (params.clearBuffer and list) {
            const float zero[4]{};
            list->ClearRenderTargetView(m_rtvHandles[dst].cpu, zero, 0, nullptr);
        }
    }
    RenderTexture(tex, params, color);
    if (dst >= 0) {
        auto* list = m_cmdList.List();
        TransitionColor(list, dst, D3D12_RESOURCE_STATE_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    return true;
}


bool RenderTarget::AutoRender(const RTRenderParams& params, const RGBAColor& color)
{
    return Render(params, color);
}

// =================================================================================================
