#include "gfxdriverstates.h"

// =================================================================================================
// TextureSlotInfo

TextureSlotInfo::TextureSlotInfo(GLenum typeTag)
    : m_typeTag(typeTag)
{
    m_srvIndices.fill(0u);
}


int TextureSlotInfo::Find(uint32_t srvIndex) const noexcept {
    if (not srvIndex) return -1;
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
        if (slotIndex < 0) return -1;
    }
    if (slotIndex >= MAX_SLOTS) return -1;
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
// GfxDriverStates

TextureSlotInfo* GfxDriverStates::FindInfo(GLenum typeTag) {
    for (auto& info : m_slotInfos)
        if (info.GetTypeTag() == typeTag)
            return &info;
    m_slotInfos.Append(TextureSlotInfo(typeTag));
    return &m_slotInfos[m_slotInfos.Length() - 1];
}


int GfxDriverStates::BoundTMU(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info) return -1;
    return (slotIndex >= 0) ? (info->Query(slotIndex) == srvIndex ? slotIndex : -1)
                             : info->Find(srvIndex);
}


int GfxDriverStates::BindTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info) return -1;
    return info->Bind(srvIndex, slotIndex);
}


bool GfxDriverStates::ReleaseTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info) return false;
    return info->Release(srvIndex, slotIndex);
}


int GfxDriverStates::GetBoundTexture(GLenum typeTag, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info) return 0;
    return int(info->Query(slotIndex));
}


int GfxDriverStates::SetBoundTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info) return -1;
    info->Update(srvIndex, slotIndex);
    return slotIndex;
}


void GfxDriverStates::ReleaseBuffers(void) noexcept {
    for (auto& info : m_slotInfos)
        info = TextureSlotInfo(info.GetTypeTag());
}

// =================================================================================================
