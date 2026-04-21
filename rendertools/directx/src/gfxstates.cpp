#include "gfxstates.h"
#include "commandlist.h"
#include "shader.h"
#include "dx12context.h"
#include "descriptor_heap.h"

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
    if (slotIndex >= m_maxUsed) m_maxUsed = slotIndex + 1;
    return slotIndex;
}


bool TextureSlotInfo::Release(uint32_t srvIndex, int slotIndex) noexcept {
    if (slotIndex >= 0) {
        if (slotIndex < MAX_SLOTS && m_srvIndices[slotIndex] == srvIndex) {
            m_srvIndices[slotIndex] = 0u;
            return true;
        }
        return false;
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

RenderState& GfxStates::ActiveState(void) noexcept {
    CommandList* cl = commandListHandler.GetCurrentCmdListObj();
    return cl ? cl->m_renderState : m_state;
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
    return (slotIndex >= 0) ? (info->Query(slotIndex) == srvIndex ? slotIndex : -1)
                             : info->Find(srvIndex);
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


void GfxStates::ReleaseBuffers(void) noexcept {
    for (auto& info : m_slotInfos)
        info = TextureSlotInfo(info.GetTypeTag());
}

// =================================================================================================
// RenderState::GetPSO — PSO creation helpers

static D3D12_BLEND ToD3DBlend(GfxOperations::BlendFactor factor) noexcept
{
    static D3D12_BLEND lut[] = { 
        D3D12_BLEND_ZERO, 
        D3D12_BLEND_ONE, 
        D3D12_BLEND_SRC_COLOR, 
        D3D12_BLEND_INV_SRC_COLOR, 
        D3D12_BLEND_SRC_ALPHA, 
        D3D12_BLEND_INV_SRC_ALPHA, 
        D3D12_BLEND_DEST_ALPHA, 
        D3D12_BLEND_INV_DEST_ALPHA, 
        D3D12_BLEND_DEST_COLOR, 
        D3D12_BLEND_INV_DEST_COLOR, 
        D3D12_BLEND_ONE 
    };
    return lut[int(factor)];
}


static D3D12_BLEND_OP ToD3DBlendOp(GfxOperations::BlendOp op) noexcept
{
    static D3D12_BLEND_OP lut[] = { 
        D3D12_BLEND_OP_ADD, 
        D3D12_BLEND_OP_SUBTRACT, 
        D3D12_BLEND_OP_REV_SUBTRACT, 
        D3D12_BLEND_OP_MIN, 
        D3D12_BLEND_OP_MAX
    };
    return lut[int(op)];
}


static D3D12_STENCIL_OP ToD3DStencilOp(GfxOperations::StencilOp op) noexcept
{
    static D3D12_STENCIL_OP lut[] = { 
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_ZERO,
        D3D12_STENCIL_OP_REPLACE, 
        D3D12_STENCIL_OP_INCR_SAT, 
        D3D12_STENCIL_OP_DECR_SAT, 
        D3D12_STENCIL_OP_INCR, 
        D3D12_STENCIL_OP_DECR 
    };
    return lut[int(op)];
}


static D3D12_COMPARISON_FUNC ToD3DCompFunc(GfxOperations::CompareFunc func) noexcept
{
    static D3D12_COMPARISON_FUNC lut[] = { 
        D3D12_COMPARISON_FUNC_NEVER, 
        D3D12_COMPARISON_FUNC_LESS, 
        D3D12_COMPARISON_FUNC_EQUAL, 
        D3D12_COMPARISON_FUNC_LESS_EQUAL, 
        D3D12_COMPARISON_FUNC_GREATER, 
        D3D12_COMPARISON_FUNC_NOT_EQUAL, 
        D3D12_COMPARISON_FUNC_GREATER_EQUAL, 
        D3D12_COMPARISON_FUNC_ALWAYS
    };
    return lut[int(func)];
}

// =================================================================================================
