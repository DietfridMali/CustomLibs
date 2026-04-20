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

static DXGI_FORMAT FormatForType(BufferInfo::eBufferType type)
{
    switch (type) {
    case BufferInfo::btDepth:
        return kDepthFormat;
    case BufferInfo::btStencil:
        return kDepthFormat;
    default:
        return kColorFormat;
    }
}


static D3D12_RESOURCE_FLAGS ResourceFlagsForType(BufferInfo::eBufferType type)
{
    if ((type == BufferInfo::btDepth) or (type == BufferInfo::btStencil))
        return D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    return D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
}


static ComPtr<ID3D12Resource> CreateRTResource(ID3D12Device* device, int w, int h, DXGI_FORMAT fmt, D3D12_RESOURCE_STATES initState, const D3D12_CLEAR_VALUE* clearVal, D3D12_RESOURCE_FLAGS flags) noexcept
{
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = UINT(w);
    rd.Height = UINT(h);
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = flags;

    ComPtr<ID3D12Resource> resource;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, clearVal, IID_PPV_ARGS(&resource));
    return resource;
}


// =================================================================================================
// BufferInfo

void BufferInfo::Init(void)
{
    m_resource.Reset();
    m_rtvHandle = {};
    m_srvHandle = {};
    m_dsvHandle = {};
    m_srvIndex = UINT32_MAX;
    m_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_type = btColor;
}


void BufferInfo::SetState(CommandList* cmdList, D3D12_RESOURCE_STATES targetState)
{
    if ((m_state == targetState) or not cmdList or not m_resource)
        return;
    cmdList->SetBarrier(m_resource.Get(), m_state, targetState);
    m_state = targetState;
}


bool BufferInfo::AllocRTV(void) {
    m_rtvHandle = descriptorHeaps.AllocRTV();
    if (not m_rtvHandle.IsValid())
        return false;
    D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
    rtvd.Format = kColorFormat;
    rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dx12Context.Device()->CreateRenderTargetView(m_resource.Get(), &rtvd, m_rtvHandle.cpu);
    return true;

}

void BufferInfo::Release(void) {
    descriptorHeaps.FreeRTV(m_rtvHandle);
    descriptorHeaps.FreeSRV(m_srvHandle);
    descriptorHeaps.FreeDSV(m_dsvHandle);
    Init();
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
    m_bufferCount = m_colorBufferCount = m_vertexBufferCount = 0;
    m_vertexBufferIndex = -1;
    m_depthBufferIndex = -1;
    m_stencilBufferIndex = -1;
    m_activeBufferIndex = 0;
    m_lastDestination = -1;
    m_pingPong = false;
    m_isAvailable = false;
    m_drawBufferGroup = dbAll;
    m_clearColor = ColorData::Invisible;
    m_bufferInfo.Reset();
}


void RenderTarget::CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType)
{
    ID3D12Device* device = dx12Context.Device();
    BufferInfo& info = m_bufferInfo[bufferIndex];
    info.Init();
    info.m_type = bufferType;

    DXGI_FORMAT fmt = FormatForType(bufferType);
    D3D12_CLEAR_VALUE cv{};
    cv.Format = fmt;

    int w = m_width * m_scale;
    int h = m_height * m_scale;

    if ((bufferType == BufferInfo::btDepth) or (bufferType == BufferInfo::btStencil)) {
        cv.DepthStencil.Depth = 1.0f;
        cv.DepthStencil.Stencil = 0;
        info.m_resource = CreateRTResource(device, w, h, fmt, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, ResourceFlagsForType(bufferType));
        if (not info.m_resource)
            return;
        info.m_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        info.m_dsvHandle = descriptorHeaps.AllocDSV();
        if (not info.m_dsvHandle.IsValid())
            return;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvd{};
        dsvd.Format = fmt;
        dsvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(info.m_resource.Get(), &dsvd, info.m_dsvHandle.cpu);
    }
    else {
        info.m_resource = CreateRTResource(device, w, h, fmt, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, ResourceFlagsForType(bufferType));
        if (not info.m_resource)
            return;
        info.m_state = D3D12_RESOURCE_STATE_RENDER_TARGET;

        info.m_rtvHandle = descriptorHeaps.AllocRTV();
        if (not info.m_rtvHandle.IsValid())
            return;
        D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
        rtvd.Format = fmt;
        rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(info.m_resource.Get(), &rtvd, info.m_rtvHandle.cpu);

        info.m_srvHandle = descriptorHeaps.AllocSRV();
        if (not info.m_srvHandle.IsValid())
            return;
        info.m_srvIndex = info.m_srvHandle.index;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.Format = fmt;
        srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvd.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(info.m_resource.Get(), &srvd, info.m_srvHandle.cpu);

        auto* list = m_cmdList->List();
        if (list)
            info.SetState(m_cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    ++m_bufferCount;
}


int RenderTarget::CreateSpecialBuffers(BufferInfo::eBufferType bufferType, int& attachmentIndex, int bufferCount)
{
    if (not bufferCount)
        return -1;
    for (int i = 0; i < bufferCount; ++i)
        CreateBuffer(m_bufferCount, attachmentIndex, bufferType);
    return m_bufferCount - bufferCount;
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
    m_bufferInfo.Resize(params.colorBufferCount + params.vertexBufferCount + params.depthBufferCount + params.stencilBufferCount);
    m_pingPong = m_colorBufferCount > 1;

    delete m_cmdList;
    m_cmdList = commandListHandler.CreateCmdList(m_name.IsEmpty() ? String("RenderTarget") : m_name);
    if (not m_cmdList)
        return false;
    if (not m_cmdList->Open(commandListHandler.CmdQueue().FrameIndex()))
        return false;

    int attachmentIndex = 0;
    for (int i = 0; i < m_colorBufferCount; ++i)
        CreateBuffer(i, attachmentIndex, BufferInfo::btColor);
    m_haveRTVs = true;

    m_vertexBufferIndex = CreateSpecialBuffers(BufferInfo::btVertex, attachmentIndex, params.vertexBufferCount);
    m_depthBufferIndex = CreateSpecialBuffers(BufferInfo::btDepth, attachmentIndex, params.depthBufferCount);
    m_stencilBufferIndex = CreateSpecialBuffers(BufferInfo::btStencil, attachmentIndex, params.stencilBufferCount);

    m_cmdList->Flush();

    int w = width * scale;
    int h = height * scale;
    m_viewport = Viewport(0, 0, w, h);
    CreateRenderArea();
    m_isAvailable = true;
    return true;
}


bool RenderTarget::AllocRTVs(void)
{
    if (m_haveRTVs)
        return true;
    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return false;
    for (int i = 0; i < m_colorBufferCount; ++i)
        if (not m_bufferInfo[i].AllocRTV())
            return false;
    return m_haveRTVs = true;
}


void RenderTarget::FreeRTVs(void)
{
    for (int i = 0; i < m_colorBufferCount; ++i) {
        descriptorHeaps.FreeRTV(m_bufferInfo[i].m_rtvHandle);
        m_bufferInfo[i].m_rtvHandle = {};
    }
    m_haveRTVs = false;
}


bool RenderTarget::AttachBuffer(int bufferIndex)
{
    if ((bufferIndex < 0) or (bufferIndex >= m_bufferCount))
        return false;
    auto* list = m_cmdList->List();
    if (not list)
        return false;
    m_bufferInfo[bufferIndex].SetState(m_cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
    return true;
}


bool RenderTarget::DetachBuffer(int bufferIndex)
{
    if ((bufferIndex < 0) or (bufferIndex >= m_bufferCount))
        return false;
    auto* list = m_cmdList->List();
    if (not list)
        return false;
    m_bufferInfo[bufferIndex].SetState(m_cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    return true;
}


void RenderTarget::Destroy(void)
{
    if (m_cmdList) {
        if (m_cmdList->IsRecording())
            m_cmdList->Close();
        delete m_cmdList;
        m_cmdList = nullptr;
    }
    for (int i = 0; i < m_bufferCount; ++i)
        m_bufferInfo[i].Release();
    m_isAvailable = false;
    m_haveRTVs = false;
    m_bufferCount = m_colorBufferCount = m_vertexBufferCount = 0;
    m_depthBufferIndex = m_stencilBufferIndex = m_vertexBufferIndex = -1;
    m_bufferInfo.Reset();
}


bool RenderTarget::SelectDrawBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup)
{
    auto* list = m_cmdList->List();
    if (not list)
        return false;

    const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[RT_MAX_COLOR_BUFFERS]{};
    int count = 0;

    if (drawBufferGroup == dbDepth) {
        for (int i = 0; i < m_colorBufferCount; ++i)
            m_bufferInfo[i].SetState(m_cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pDSV = DepthBufferHandle();
    }
    else if (drawBufferGroup == dbSingle) {
        m_drawBufferGroup = dbSingle;
        if ((bufferIndex < 0) or (bufferIndex >= m_bufferInfo.Length()))
            return false;
        m_activeBufferIndex = bufferIndex;
        m_bufferInfo[bufferIndex].SetState(m_cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
        rtvs[count++] = m_bufferInfo[bufferIndex].m_rtvHandle.cpu;
        for (int i = 0; i < m_colorBufferCount; ++i)
            if (i != bufferIndex)
                m_bufferInfo[i].SetState(m_cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pDSV = DepthBufferHandle();
    }
    else {
        m_activeBufferIndex = -1;
        m_drawBufferGroup = (drawBufferGroup == dbNone) ? dbAll : drawBufferGroup;
        if (m_drawBufferGroup == dbAll) {
            for (int i = 0; i < m_colorBufferCount; ++i) {
                m_bufferInfo[i].SetState(m_cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                rtvs[count++] = m_bufferInfo[i].m_rtvHandle.cpu;
            }
            pDSV = DepthBufferHandle();
        }
        else if (m_drawBufferGroup == dbColor) {
            for (int i = 0; i < m_colorBufferCount; ++i) {
                m_bufferInfo[i].SetState(m_cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                rtvs[count++] = m_bufferInfo[i].m_rtvHandle.cpu;
            }
            pDSV = DepthBufferHandle();
            for (int i = m_colorBufferCount; i < m_bufferCount; ++i)
                m_bufferInfo[i].SetState(m_cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        else if (m_drawBufferGroup == dbExtra) {
            for (int i = 0; i < m_colorBufferCount; ++i)
                m_bufferInfo[i].SetState(m_cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            for (int i = m_colorBufferCount; i < m_bufferCount; ++i) {
                m_bufferInfo[i].SetState(m_cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                rtvs[count++] = m_bufferInfo[i].m_rtvHandle.cpu;
            }
        }
    }

    if (count > 0)
        list->OMSetRenderTargets(count, rtvs, FALSE, pDSV);
    else if (pDSV)
        list->OMSetRenderTargets(0, nullptr, FALSE, pDSV);

    return true;
}


bool RenderTarget::SetDrawBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool reenable)
{
    if (not SelectDrawBuffers(bufferIndex, drawBufferGroup))
        return false;
    if (not reenable)
        baseRenderer.TrackDrawBuffers(this);
    return true;
}


bool RenderTarget::DepthBufferIsActive(int bufferIndex, eDrawBufferGroups drawBufferGroup)
{
    if (m_depthBufferIndex < 0)
        return false;
    if (bufferIndex >= 0)
        return (m_bufferInfo[bufferIndex].m_type == BufferInfo::btColor) or (m_bufferInfo[bufferIndex].m_type == BufferInfo::btDepth);
    return (m_drawBufferGroup == dbAll) or (m_drawBufferGroup == dbColor) or (m_drawBufferGroup == dbDepth);
}


bool RenderTarget::EnableBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable)
{
    if (not SetDrawBuffers(bufferIndex, drawBufferGroup, reenable))
        return false;
    gfxDriverStates.SetDepthTest(DepthBufferIsActive(bufferIndex, drawBufferGroup));
    Clear(bufferIndex, drawBufferGroup, clear);
    return true;
}


bool RenderTarget::Enable(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable)
{
    if (not m_isAvailable)
        return false;
    if (not AllocRTVs())
        return false;
    m_activeBufferIndex = (bufferIndex < 0) ? 0 : (bufferIndex % m_bufferCount);
    m_drawBufferGroup = drawBufferGroup;

    bool wasRecording = m_cmdList->IsRecording();
    if (not m_cmdList->Open(commandListHandler.CmdQueue().FrameIndex()))
        return false;
    if (not wasRecording)
        SetViewport();

    return EnableBuffers(bufferIndex, drawBufferGroup, clear, reenable);
}


void RenderTarget::Disable(bool flush, bool restoreDrawBuffer)
{
    if (IsEnabled()) {
        auto* list = m_cmdList->List();
        for (int i = 0; i < m_colorBufferCount; ++i)
            m_bufferInfo[i].SetState(m_cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (flush) {
            m_cmdList->Flush();
            FreeRTVs();
        }
        else
            m_cmdList->Close();
        if (restoreDrawBuffer)
            baseRenderer.RestoreDrawBuffer();
    }
}


uint32_t& RenderTarget::BufferHandle(int bufferIndex)
{
    if (bufferIndex >= 0 and bufferIndex < m_colorBufferCount)
        return m_bufferInfo[bufferIndex].m_srvIndex;
    static uint32_t invalid = UINT32_MAX;
    return invalid;
}


void RenderTarget::SetViewport(bool flipVertically) noexcept
{
    baseRenderer.SetViewport(m_viewport, 0, 0, flipVertically);
}


void RenderTarget::Fill(RGBAColor color)
{
    auto* list = m_cmdList->List();
    if (not list)
        return;
    float c[4] = { color.R(), color.G(), color.B(), color.A() };
    for (int i = 0; i < m_colorBufferCount; ++i)
        if (m_bufferInfo[i].m_rtvHandle.IsValid())
            list->ClearRenderTargetView(m_bufferInfo[i].m_rtvHandle.cpu, c, 0, nullptr);
}


void RenderTarget::Clear(int bufferIndex, eDrawBufferGroups /*drawBufferGroup*/, bool clear)
{
    if (not clear)
        return;
    auto* list = m_cmdList->List();
    if (not list)
        return;
    if (bufferIndex < 0) {
        for (int i = 0; i < m_colorBufferCount; ++i)
            if (m_bufferInfo[i].m_rtvHandle.IsValid())
                list->ClearRenderTargetView(m_bufferInfo[i].m_rtvHandle.cpu, m_clearColor.Data(), 0, nullptr);
    }
    else if ((bufferIndex < m_colorBufferCount) and m_bufferInfo[bufferIndex].m_rtvHandle.IsValid()) {
        list->ClearRenderTargetView(m_bufferInfo[bufferIndex].m_rtvHandle.cpu, m_clearColor.Data(), 0, nullptr);
    }
    if (HaveDepthBuffer(true))
        list->ClearDepthStencilView(m_bufferInfo[m_depthBufferIndex].m_dsvHandle.cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}


Texture* RenderTarget::GetAsTexture(const RTRenderParams& params, int /*tmuIndex*/)
{
    BufferInfo& info = m_bufferInfo[params.source % m_bufferCount];
    if (not info.m_resource)
        return nullptr;
    m_renderTexture.m_handle = info.m_srvIndex;
    m_renderTexture.m_resource = info.m_resource;
    m_renderTexture.m_isValid = true;
    return &m_renderTexture;
}


void RenderTarget::ClearStencil(void)
{
    if ((m_depthBufferIndex < 0) or not m_bufferInfo[m_depthBufferIndex].m_dsvHandle.IsValid())
        return;
    auto* list = m_cmdList->List();
    if (not list)
        return;
    list->ClearDepthStencilView(m_bufferInfo[m_depthBufferIndex].m_dsvHandle.cpu, D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
}


Texture* RenderTarget::GetDepthTexture(void)
{
    return nullptr;
}


bool RenderTarget::UpdateTransformation(const RTRenderParams& params)
{
    bool haveTransformation = false;
    if (params.centerOrigin) {
        haveTransformation = true;
        baseRenderer.Translate(0.5, 0.5, 0);
    }
    if (params.rotation) {
        haveTransformation = true;
        baseRenderer.Rotate(params.rotation, 0, 0, 1);
    }
#if 1
    if (params.flipVertically) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, params.scale * params.flipVertically, 1);
    }
    else if (params.source & 1) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, -params.scale, 1);
    }
#endif
    else if (params.scale != 1.0f) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, params.scale, 1);
    }
    return haveTransformation;
}


bool RenderTarget::RenderAsTexture(Texture* source, const RTRenderParams& params, const RGBAColor& color)
{
    if (params.destination < 0) {
        gfxDriverStates.SetBlending(1);
    }
    else {
        if (not Enable(params.destination, RenderTarget::dbSingle, true, true))
            return false;
        m_lastDestination = params.destination;
        gfxDriverStates.SetBlending(0);
    }
    baseRenderer.PushMatrix();
    bool applyTransformation = UpdateTransformation(params);
    gfxDriverStates.SetDepthTest(0);
    gfxDriverStates.SetDepthWrite(0);
    gfxDriverStates.DepthFunc(GfxOperations::CompareFunc::Always);
    gfxDriverStates.SetFaceCulling(0);
    if (params.shader) {
        if (applyTransformation)
            params.shader->UpdateMatrices();
        m_viewportArea.Render(params.shader, source);
    }
    else {
        if (params.premultiply)
            m_viewportArea.Premultiply();
        m_viewportArea.Render(nullptr, source, color);
    }
    baseRenderer.PopMatrix();
    return true;
}


bool RenderTarget::Render(const RTRenderParams& params, const RGBAColor& color)
{
    if (params.destination >= 0)
        m_lastDestination = params.destination;
    return RenderAsTexture((params.source == params.destination) ? nullptr : GetAsTexture(params), params, color);
}


void RenderTarget::CreateRenderArea(void)
{
    m_viewportArea.Setup(BaseQuad::defaultVertices[BaseQuad::voCenter], BaseQuad::defaultTexCoords[BaseQuad::tcRegular]);
}


bool RenderTarget::AutoRender(const RTRenderParams& params, const RGBAColor& color)
{
    return Render(params, color);
}

// =================================================================================================
