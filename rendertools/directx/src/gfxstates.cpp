#include "gfxstates.h"
#include "commandlist.h"
#include "shader.h"
#include "dx12context.h"
#include "descriptor_heap.h"
#include "gfxrenderer.h"
#include "base_displayhandler.h"
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


void GfxStates::SetViewport(const GfxTypes::Int left, const GfxTypes::Int top, const GfxTypes::Int right, const GfxTypes::Int bottom) noexcept {
    auto* list = commandListHandler.CurrentGfxList();
    if (list) {
        D3D12_VIEWPORT vp{};
        vp.TopLeftX = float(left);
        vp.TopLeftY = float(top);
        GfxTypes::Int windowWidth = right - left + 1;
        GfxTypes::Int windowHeight = bottom - top + 1;
        vp.Width = float(windowWidth);
        vp.Height = float(windowHeight);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        list->RSSetViewports(1, &vp);
        D3D12_RECT scissorArea{ 0, 0, windowWidth, windowHeight };
        list->RSSetScissorRects(1, &scissorArea);
        m_viewport[0] = left;
        m_viewport[1] = top;
        m_viewport[2] = right;
        m_viewport[3] = bottom;
    }
}


void GfxStates::SetDrawBuffers(const DrawBufferList& drawBuffers) { 
    //no op
}

void GfxStates::ClearError(void) noexcept {
    // no op
}


bool GfxStates::CheckError(const char* operation) noexcept {
#ifdef NDEBUG
    return true;
#else
    if (dx12Context.DrainMessages() > 0)
        return false;
    return true;
#endif
}

// =================================================================================================
