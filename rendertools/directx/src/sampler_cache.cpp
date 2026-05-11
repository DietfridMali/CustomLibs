#include <cstdio>
#include <cstring>

#include "sampler_cache.h"
#include "descriptor_heap.h"
#include "dx12context.h"

// =================================================================================================

SamplerCache::SamplerCache(void) noexcept {
    m_cache.SetComparator(&SamplerCache::Compare);
}


void SamplerCache::Destroy(void) noexcept {
    m_cache.Clear();
}


uint32_t SamplerCache::GetSlot(const TextureSampling& s) noexcept {
    if (uint32_t* found = m_cache.Find(s))
        return *found;

    ID3D12Device* device = dx12Context.Device();
    if (not device)
        return UINT32_MAX;

    DescriptorHandle h = descriptorHeaps.AllocSampler();
    if (not h.IsValid()) {
        fprintf(stderr, "SamplerCache: sampler heap full, cannot create sampler\n");
        return UINT32_MAX;
    }

    D3D12_SAMPLER_DESC desc = ToD3D12Desc(s);
    device->CreateSampler(&desc, h.cpuHandle);

    m_cache.Insert(TextureSampling(s), uint32_t(h.index));
    return h.index;
}


int SamplerCache::Compare(void* /*context*/, const TextureSampling& a, const TextureSampling& b) {
    return std::memcmp(&a, &b, sizeof(TextureSampling));
}


D3D12_SAMPLER_DESC SamplerCache::ToD3D12Desc(const TextureSampling& s) noexcept {
    D3D12_SAMPLER_DESC d{};

    const bool useCompare = (s.compareFunc != GfxOperations::CompareFunc::Always);
    const bool useAniso   = (s.maxAnisotropy > 1.0f);

    // D3D12_FILTER bits — bit0=mip, bit2=mag, bit4=min, bit7=comparison.
    // Anisotropic uses dedicated values that override the per-stage bits.
    if (useAniso) {
        d.Filter = useCompare ? D3D12_FILTER_COMPARISON_ANISOTROPIC
                              : D3D12_FILTER_ANISOTROPIC;
    }
    else {
        UINT bits = 0;
        if (s.mipMode == GfxMipMode::Linear)
            bits |= 0x01;
        if (s.magFilter == GfxFilterMode::Linear)
            bits |= 0x04;
        if (s.minFilter == GfxFilterMode::Linear)
            bits |= 0x10;
        if (useCompare)
            bits |= 0x80;
        d.Filter = D3D12_FILTER(bits);
    }

    auto wrap = [](GfxWrapMode m) -> D3D12_TEXTURE_ADDRESS_MODE {
        switch (m) {
            case GfxWrapMode::Repeat:
                return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            case GfxWrapMode::ClampToEdge:
                return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        }
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    };
    d.AddressU = wrap(s.wrapU);
    d.AddressV = wrap(s.wrapV);
    d.AddressW = wrap(s.wrapW);

    d.MipLODBias    = s.mipLodBias;
    d.MaxAnisotropy = UINT(s.maxAnisotropy);

    auto cmpFunc = [](GfxOperations::CompareFunc f) -> D3D12_COMPARISON_FUNC {
        switch (f) {
            case GfxOperations::CompareFunc::Never:
                return D3D12_COMPARISON_FUNC_NEVER;
            case GfxOperations::CompareFunc::Less:
                return D3D12_COMPARISON_FUNC_LESS;
            case GfxOperations::CompareFunc::Equal:
                return D3D12_COMPARISON_FUNC_EQUAL;
            case GfxOperations::CompareFunc::LessEqual:
                return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case GfxOperations::CompareFunc::Greater:
                return D3D12_COMPARISON_FUNC_GREATER;
            case GfxOperations::CompareFunc::NotEqual:
                return D3D12_COMPARISON_FUNC_NOT_EQUAL;
            case GfxOperations::CompareFunc::GreaterEqual:
                return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            case GfxOperations::CompareFunc::Always:
                return D3D12_COMPARISON_FUNC_ALWAYS;
        }
        return D3D12_COMPARISON_FUNC_ALWAYS;
    };
    // D3D12 warnt, wenn ComparisonFunc != NEVER bei Nicht-Compare-Filter gesetzt ist.
    // Bei Compare-Sampler den eigentlichen Vergleich nehmen, sonst NEVER (= ignored).
    d.ComparisonFunc = useCompare ? cmpFunc(s.compareFunc) : D3D12_COMPARISON_FUNC_NEVER;

    d.BorderColor[0] = s.borderColor[0];
    d.BorderColor[1] = s.borderColor[1];
    d.BorderColor[2] = s.borderColor[2];
    d.BorderColor[3] = s.borderColor[3];

    // When mip-mapping is disabled we clamp LOD to the base level so the chosen
    // mip filter (which we still encode for completeness) becomes irrelevant.
    if (s.mipMode == GfxMipMode::None) {
        d.MinLOD = 0.0f;
        d.MaxLOD = 0.0f;
    }
    else {
        d.MinLOD = s.minLOD;
        d.MaxLOD = s.maxLOD;
    }

    return d;
}

// =================================================================================================
