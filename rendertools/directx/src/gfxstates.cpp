#include "gfxstates.h"
#include "commandlist.h"
#include "shader.h"
#include "dx12context.h"
#include "descriptor_heap.h"
#include "gfxrenderer.h"
#include "base_displayhandler.h"
#include "rendertarget.h"
#include "base_renderer.h"
#include "commandlist.h"

#include <cstdio>

// =================================================================================================
// TextureSlotInfo

TextureSlotInfo::TextureSlotInfo(GLenum typeTag)
    : m_typeTag(typeTag)
{
    m_srvIndices.fill(0u);
}


int TextureSlotInfo::Find(uint32_t srvIndex) const noexcept {
    if (not srvIndex)
        return -1;
    for (int i = 0; i < m_maxUsed; ++i)
        if (m_srvIndices[i] == srvIndex)
            return i;
    return -1;
}


int TextureSlotInfo::Bind(uint32_t srvIndex, int slotIndex) noexcept {
    if (slotIndex < 0) {
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (not m_srvIndices[i]) { slotIndex = i; break; }
        }
        if (slotIndex < 0)
            return -1;
    }
    if (slotIndex >= MAX_SLOTS)
        return -1;
    m_srvIndices[slotIndex] = srvIndex;
    if (slotIndex >= m_maxUsed) 
        m_maxUsed = slotIndex + 1;
    return slotIndex;
}


bool TextureSlotInfo::Release(uint32_t srvIndex, int slotIndex) noexcept {
    if (slotIndex >= 0) {
        if (slotIndex < MAX_SLOTS && m_srvIndices[slotIndex] != srvIndex) 
            return false;
        m_srvIndices[slotIndex] = 0u;
        return true;
    }
    bool released = false;
    for (int i = 0; i < m_maxUsed; ++i) {
        if (m_srvIndices[i] == srvIndex) {
            m_srvIndices[i] = 0u;
            released = true;
        }
    }
    return released;
}


uint32_t TextureSlotInfo::Query(int slotIndex) const noexcept {
    return (slotIndex >= 0 && slotIndex < MAX_SLOTS) ? m_srvIndices[slotIndex] : 0u;
}


bool TextureSlotInfo::Update(uint32_t srvIndex, int slotIndex) noexcept {
    if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return false;
    m_srvIndices[slotIndex] = srvIndex;
    if (slotIndex >= m_maxUsed) m_maxUsed = slotIndex + 1;
    return true;
}

// =================================================================================================
// GfxStates

RenderStates& GfxStates::ActiveState(void) noexcept {
    return baseRenderer.RenderStates();
}


TextureSlotInfo* GfxStates::FindInfo(GLenum typeTag) {
    // Nothing to find once this singleton has been torn down - the list is gone, and iterating it
    // reads freed memory, while appending to it would be worse still. Textures and render targets
    // owned by other statics are destroyed after it and drop their bindings here on the way out;
    // for those there is nothing left to drop.
    if (IsDestroyed())
        return nullptr;
    for (auto& info : m_slotInfos)
        if (info.GetTypeTag() == typeTag)
            return &info;
    m_slotInfos.Append(TextureSlotInfo(typeTag));
    return &m_slotInfos[m_slotInfos.Length() - 1];
}


int GfxStates::BoundTMU(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    return (slotIndex >= 0) ? (info->Query(slotIndex) == srvIndex ? slotIndex : -1) : info->Find(srvIndex);
}


int GfxStates::BindTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    return info->Bind(srvIndex, slotIndex);
}


bool GfxStates::ReleaseTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return false;
    return info->Release(srvIndex, slotIndex);
}


int GfxStates::GetBoundTexture(GLenum typeTag, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return 0;
    return int(info->Query(slotIndex));
}


int GfxStates::SetBoundTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    info->Update(srvIndex, slotIndex);
    return slotIndex;
}


void GfxStates::ClearColorBuffers(D3D12_CPU_DESCRIPTOR_HANDLE rtv) noexcept {
    auto* list = commandListHandler.CurrentGfxList();
    if (list)
        list->ClearRenderTargetView(rtv, m_clearColor.Data(), 0, nullptr);
}


void GfxStates::ClearColorBuffers(void) noexcept {
    RenderTarget* rt = baseRenderer.GetActiveBuffer();
    if (not rt) {
        ClearBackBuffer(m_clearColor);
        return;
    }
    RGBAColor clearColor = rt->m_clearColor;
    rt->SetClearColor(m_clearColor);
    rt->ClearColorBuffers();
    rt->SetClearColor(clearColor);
}


void GfxStates::ClearDepthBuffer(float clearValue) noexcept {
    RenderTarget* rt = baseRenderer.GetActiveBuffer();
    if (rt)
        rt->ClearDepthBuffer(clearValue);
}


void GfxStates::ClearStencilBuffer(int clearValue) noexcept {
    RenderTarget* rt = baseRenderer.GetActiveBuffer();
    if (rt)
        rt->ClearStencilBuffer(clearValue);
}


void GfxStates::ClearBackBuffer(const RGBAColor& color) noexcept {
    auto* list = commandListHandler.CurrentGfxList();
    if (list)
        list->ClearRenderTargetView(baseDisplayHandler.CurrentRTV(), color.Data(), 0, nullptr);
}


void GfxStates::ClearDepthBuffer(D3D12_CPU_DESCRIPTOR_HANDLE dsv, float clearValue) noexcept {
    auto* list = commandListHandler.CurrentGfxList();
    if (list)
        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, clearValue, 0, 0, nullptr);
}


void GfxStates::ClearStencilBuffer(D3D12_CPU_DESCRIPTOR_HANDLE dsv, int clearValue) noexcept {
    auto* list = commandListHandler.CurrentGfxList();
    if (list)
        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_STENCIL, 0.0f, uint8_t(clearValue), 0, nullptr);
}


void GfxStates::ClearSkyMaps(RenderTarget* rt) noexcept {
    if (rt == nullptr or (rt->m_computeBufferCount <= 0))
        return;

    // Open a temporary CL if none is active. ClearRenderTargetView + ResourceBarrier require a
    // recording command list.
    void* opHandle = nullptr;
    CommandList* cmdList = commandListHandler.CurrentCmdList();
    if (cmdList == nullptr) {
        opHandle = baseRenderer.StartOperation("GfxStates::ClearSkyMaps", false);
        if (opHandle == nullptr)
            return;
        cmdList = commandListHandler.CurrentCmdList();
        if (cmdList == nullptr) {
            baseRenderer.FinishOperation(opHandle);
            return;
        }
    }

    const FLOAT zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    auto* list = cmdList->GfxList();
    for (int i = 0; i < rt->m_computeBufferCount; ++i) {
        BufferInfo& bi = rt->m_bufferInfo[rt->m_computeBufferIndex + i];
        bi.SetState(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ClearRenderTargetView(bi.m_rtv.CPUHandle(), zero, 0, nullptr);
        bi.SetState(cmdList, kShaderReadState);
    }

    if (opHandle != nullptr)
        baseRenderer.FinishOperation(opHandle);
}


void GfxStates::SetMemoryBarrier(GfxTypes::Bitfield /*barriers*/) noexcept {
    auto* list = commandListHandler.CurrentGfxList();
    if (list) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = nullptr;
        list->ResourceBarrier(1, &b);
    }
}


void GfxStates::Finish(void) noexcept {
    commandListHandler.CmdQueue().WaitIdle();
}


void GfxStates::ReleaseBuffers(void) noexcept {
    for (auto& info : m_slotInfos)
        info = TextureSlotInfo(info.GetTypeTag());
}


void GfxStates::SetViewport(const GfxTypes::Int left, const GfxTypes::Int top, const GfxTypes::Int width, const GfxTypes::Int height) noexcept {
    m_viewport[0] = left;
    m_viewport[1] = top;
    m_viewport[2] = width;
    m_viewport[3] = height;
    m_scissor[0] = 0;
    m_scissor[1] = 0;
    m_scissor[2] = left + width;
    m_scissor[3] = top + height;
    auto* list = commandListHandler.CurrentGfxList();
    if (list) {
        D3D12_VIEWPORT vp{};
        vp.TopLeftX = float(left);
        vp.TopLeftY = float(top);
        vp.Width = float(width);
        vp.Height = float(height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        list->RSSetViewports(1, &vp);
        D3D12_RECT scissorArea{ 0, 0, left + width, top + height };
        list->RSSetScissorRects(1, &scissorArea);
    }
}


void GfxStates::SetScissor(const GfxTypes::Int left, const GfxTypes::Int top, const GfxTypes::Int width, const GfxTypes::Int height) noexcept {
    m_scissor[0] = left;
    m_scissor[1] = top;
    m_scissor[2] = width;
    m_scissor[3] = height;
    auto* list = commandListHandler.CurrentGfxList();
    if (list) {
        D3D12_RECT scissorArea{ left, top, left + width, top + height };
        list->RSSetScissorRects(1, &scissorArea);
    }
}


void GfxStates::RestoreViewport(void) noexcept {
    if ((m_viewport[2] <= 0) or (m_viewport[3] <= 0))
        return;
    GfxTypes::Int scissor[4] = { m_scissor[0], m_scissor[1], m_scissor[2], m_scissor[3] };
    SetViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    SetScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
}


int GfxStates::MaxTextureUnits(void) noexcept {
    return int(Shader::kSrvSlots);
}


String GfxStates::DeviceName(void) {
    char buffer[128] = "";
    if (dx12Context.m_adapter) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(dx12Context.m_adapter->GetDesc1(&desc))) {
            size_t i = 0;
            for (; (i < sizeof(buffer) - 1) and desc.Description[i]; ++i)
                buffer[i] = (desc.Description[i] < 128) ? char(desc.Description[i]) : '?';
            buffer[i] = '\0';
        }
    }
    return String(buffer);
}


void GfxStates::SetDrawBuffers(const DrawBufferList& drawBuffers) { 
    //no op
}

void GfxStates::ClearError(void) noexcept {
    // no op
}


bool GfxStates::CheckError(const char* operation) noexcept {
#if DBG_DIRECTX
    if (dx12Context.DrainMessages(false) > 0)
        return false;
#endif
    return true;
}

// =================================================================================================
