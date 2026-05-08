#include <cstdio>
#include <cstring>

#include "sampler_cache.h"
#include "vkcontext.h"

// =================================================================================================

SamplerCache::SamplerCache(void) noexcept {
    m_cache.SetComparator(&SamplerCache::Compare);
}


void SamplerCache::Destroy(void) noexcept {
    VkDevice device = vkContext.Device();
    if (device != VK_NULL_HANDLE) {
        for (auto& s : m_samplers) {
            if (s != VK_NULL_HANDLE)
                vkDestroySampler(device, s, nullptr);
        }
    }
    m_samplers.Reset();
    m_cache.Clear();
}


VkSampler SamplerCache::GetSampler(const TextureSampling& s) noexcept {
    if (VkSampler* found = m_cache.Find(s))
        return *found;

    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkSamplerCreateInfo info = ToVulkanInfo(s);
    VkSampler sampler = VK_NULL_HANDLE;
    VkResult res = vkCreateSampler(device, &info, nullptr, &sampler);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "SamplerCache::GetSampler: vkCreateSampler failed (%d)\n", (int)res);
        return VK_NULL_HANDLE;
    }

    m_cache.Insert(TextureSampling(s), sampler);
    m_samplers.Append(sampler);
    return sampler;
}


int SamplerCache::Compare(void* /*context*/, const TextureSampling& a, const TextureSampling& b) {
    return std::memcmp(&a, &b, sizeof(TextureSampling));
}

// -------------------------------------------------------------------------------------------------
// Mapping helpers — TextureSampling enums to Vulkan enums.

static VkFilter ToVkFilter(GfxFilterMode m) noexcept {
    return (m == GfxFilterMode::Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}


static VkSamplerMipmapMode ToVkMipMode(GfxMipMode m) noexcept {
    return (m == GfxMipMode::Linear) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}


static VkSamplerAddressMode ToVkWrap(GfxWrapMode m) noexcept {
    switch (m) {
        case GfxWrapMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case GfxWrapMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}


static VkCompareOp ToVkCompareOp(GfxOperations::CompareFunc f) noexcept {
    switch (f) {
        case GfxOperations::CompareFunc::Never:        return VK_COMPARE_OP_NEVER;
        case GfxOperations::CompareFunc::Less:         return VK_COMPARE_OP_LESS;
        case GfxOperations::CompareFunc::Equal:        return VK_COMPARE_OP_EQUAL;
        case GfxOperations::CompareFunc::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case GfxOperations::CompareFunc::Greater:      return VK_COMPARE_OP_GREATER;
        case GfxOperations::CompareFunc::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
        case GfxOperations::CompareFunc::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case GfxOperations::CompareFunc::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}


// Vulkan border colors are an enum with three standard values (TRANSPARENT_BLACK / OPAQUE_BLACK /
// OPAQUE_WHITE). Detect the closest match from TextureSampling.borderColor[4]; for arbitrary
// floats we'd need VK_EXT_custom_border_color, which is not enabled here. Default = OPAQUE_BLACK.
static VkBorderColor ToVkBorderColor(const float bc[4]) noexcept {
    const bool transparent = (bc[3] == 0.0f);
    const bool white = (bc[0] == 1.0f) and (bc[1] == 1.0f) and (bc[2] == 1.0f) and (bc[3] == 1.0f);
    if (transparent)
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (white)
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
}


VkSamplerCreateInfo SamplerCache::ToVulkanInfo(const TextureSampling& s) noexcept {
    VkSamplerCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = ToVkFilter(s.magFilter);
    info.minFilter = ToVkFilter(s.minFilter);
    info.mipmapMode = ToVkMipMode(s.mipMode);
    info.addressModeU = ToVkWrap(s.wrapU);
    info.addressModeV = ToVkWrap(s.wrapV);
    info.addressModeW = ToVkWrap(s.wrapW);
    info.mipLodBias = s.mipLodBias;

    const bool useAniso = (s.maxAnisotropy > 1.0f);
    info.anisotropyEnable = useAniso ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = useAniso ? s.maxAnisotropy : 1.0f;

    const bool useCompare = (s.compareFunc != GfxOperations::CompareFunc::Always);
    info.compareEnable = useCompare ? VK_TRUE : VK_FALSE;
    info.compareOp = useCompare ? ToVkCompareOp(s.compareFunc) : VK_COMPARE_OP_NEVER;

    if (s.mipMode == GfxMipMode::None) {
        // Match DX12: clamp LOD to base level so mip filter is irrelevant.
        info.minLod = 0.0f;
        info.maxLod = 0.0f;
    }
    else {
        info.minLod = s.minLOD;
        info.maxLod = s.maxLOD;
    }

    info.borderColor = ToVkBorderColor(s.borderColor);
    info.unnormalizedCoordinates = VK_FALSE;
    return info;
}

// =================================================================================================
