#include "gfxdriverstates.h"
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
// GfxDriverStates

RenderState& GfxDriverStates::ActiveState(void) noexcept {
    CommandList* cl = commandListHandler.GetCurrentCmdListObj();
    return cl ? cl->m_renderState : m_state;
}


TextureSlotInfo* GfxDriverStates::FindInfo(GLenum typeTag) {
    for (auto& info : m_slotInfos)
        if (info.GetTypeTag() == typeTag)
            return &info;
    m_slotInfos.Append(TextureSlotInfo(typeTag));
    return &m_slotInfos[m_slotInfos.Length() - 1];
}


int GfxDriverStates::BoundTMU(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    return (slotIndex >= 0) ? (info->Query(slotIndex) == srvIndex ? slotIndex : -1)
                             : info->Find(srvIndex);
}


int GfxDriverStates::BindTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    return info->Bind(srvIndex, slotIndex);
}


bool GfxDriverStates::ReleaseTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return false;
    return info->Release(srvIndex, slotIndex);
}


int GfxDriverStates::GetBoundTexture(GLenum typeTag, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return 0;
    return int(info->Query(slotIndex));
}


int GfxDriverStates::SetBoundTexture(GLenum typeTag, uint32_t srvIndex, int slotIndex) {
    TextureSlotInfo* info = FindInfo(typeTag);
    if (not info)
        return -1;
    info->Update(srvIndex, slotIndex);
    return slotIndex;
}


void GfxDriverStates::ReleaseBuffers(void) noexcept {
    for (auto& info : m_slotInfos)
        info = TextureSlotInfo(info.GetTypeTag());
}

// =================================================================================================
// RenderState::GetPSO — PSO creation helpers

static D3D12_BLEND ToD3DBlend(GfxOperations::BlendFactor factor) noexcept
{
    using enum GfxOperations::BlendFactor;
    switch (factor) {
        case Zero:        return D3D12_BLEND_ZERO;
        case One:         return D3D12_BLEND_ONE;
        case SrcColor:    return D3D12_BLEND_SRC_COLOR;
        case InvSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
        case SrcAlpha:    return D3D12_BLEND_SRC_ALPHA;
        case InvSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case DstAlpha:    return D3D12_BLEND_DEST_ALPHA;
        case InvDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
        case DstColor:    return D3D12_BLEND_DEST_COLOR;
        case InvDstColor: return D3D12_BLEND_INV_DEST_COLOR;
        default:          return D3D12_BLEND_ONE;
    }
}


static D3D12_BLEND_OP ToD3DBlendOp(GfxOperations::BlendOp op) noexcept
{
    using enum GfxOperations::BlendOp;
    switch (op) {
        case Add:         return D3D12_BLEND_OP_ADD;
        case Subtract:    return D3D12_BLEND_OP_SUBTRACT;
        case RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case Min:         return D3D12_BLEND_OP_MIN;
        case Max:         return D3D12_BLEND_OP_MAX;
        default:          return D3D12_BLEND_OP_ADD;
    }
}


static D3D12_STENCIL_OP ToD3DStencilOp(GfxOperations::StencilOp op) noexcept
{
    using enum GfxOperations::StencilOp;
    switch (op) {
        case Zero:    return D3D12_STENCIL_OP_ZERO;
        case Replace: return D3D12_STENCIL_OP_REPLACE;
        case IncrSat: return D3D12_STENCIL_OP_INCR_SAT;
        case DecrSat: return D3D12_STENCIL_OP_DECR_SAT;
        case Incr:    return D3D12_STENCIL_OP_INCR;
        case Decr:    return D3D12_STENCIL_OP_DECR;
        default:      return D3D12_STENCIL_OP_KEEP;
    }
}


static D3D12_COMPARISON_FUNC ToD3DCompFunc(GfxOperations::CompareFunc func) noexcept
{
    using enum GfxOperations::CompareFunc;
    switch (func) {
        case Never:        return D3D12_COMPARISON_FUNC_NEVER;
        case Less:         return D3D12_COMPARISON_FUNC_LESS;
        case Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
        case LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case Greater:      return D3D12_COMPARISON_FUNC_GREATER;
        case NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
        default:           return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    }
}


ID3D12PipelineState* RenderState::GetPSO(Shader* shader) noexcept
{
    if (not shader or not shader->IsValid())
        return nullptr;

    PsoCacheKey key{ shader, *this };
    if (auto* cached = CommandList::GetPsoCache().Find(key))
        return cached->Get();

    ID3D12Device* device = dx12Context.Device();
    if (not device or not shader->m_rootSignature or not shader->m_vsBlob or not shader->m_psBlob)
        return nullptr;

    // Rasterizer
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = (cullMode == GfxOperations::FaceCull::Back)
        ? D3D12_CULL_MODE_BACK
        : (cullMode == GfxOperations::FaceCull::Front)
            ? D3D12_CULL_MODE_FRONT
            : D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = (frontFace == GfxOperations::Winding::CCW) ? TRUE : FALSE;
    rast.DepthClipEnable = TRUE;
    rast.MultisampleEnable = FALSE;

    // Blend
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = colorMask;
    if (blendEnable) {
        blend.RenderTarget[0].BlendEnable    = TRUE;
        blend.RenderTarget[0].SrcBlend       = ToD3DBlend(blendSrcRGB);
        blend.RenderTarget[0].DestBlend      = ToD3DBlend(blendDstRGB);
        blend.RenderTarget[0].BlendOp        = ToD3DBlendOp(blendOpRGB);
        blend.RenderTarget[0].SrcBlendAlpha  = ToD3DBlend(blendSrcAlpha);
        blend.RenderTarget[0].DestBlendAlpha = ToD3DBlend(blendDstAlpha);
        blend.RenderTarget[0].BlendOpAlpha   = ToD3DBlendOp(blendOpAlpha);
    }

    // Depth / stencil
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = depthTest ? TRUE : FALSE;
    ds.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = ToD3DCompFunc(depthFunc);
    ds.StencilEnable = stencilTest ? TRUE : FALSE;
    ds.StencilReadMask = stencilMask;
    ds.StencilWriteMask = stencilMask;
    ds.FrontFace = { ToD3DStencilOp(stencilSFail),
                     ToD3DStencilOp(stencilDPFail),
                     ToD3DStencilOp(stencilDPPass),
                     ToD3DCompFunc(stencilFunc) };
    ds.BackFace  = { ToD3DStencilOp(stencilBackSFail),
                     ToD3DStencilOp(stencilBackDPFail),
                     ToD3DStencilOp(stencilBackDPPass),
                     ToD3DCompFunc(stencilFunc) };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature     = shader->m_rootSignature.Get();
    psoDesc.VS                 = { shader->m_vsBlob->GetBufferPointer(), shader->m_vsBlob->GetBufferSize() };
    psoDesc.PS                 = { shader->m_psBlob->GetBufferPointer(), shader->m_psBlob->GetBufferSize() };
    if (shader->m_gsBlob)
        psoDesc.GS             = { shader->m_gsBlob->GetBufferPointer(), shader->m_gsBlob->GetBufferSize() };
    psoDesc.InputLayout        = { shader->m_vsInputLayout.data(), UINT(shader->m_vsInputLayout.size()) };
    psoDesc.RasterizerState    = rast;
    psoDesc.BlendState         = blend;
    psoDesc.DepthStencilState  = ds;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets   = 1;
    psoDesc.RTVFormats[0]      = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat          = (ds.DepthEnable or ds.StencilEnable)
                               ? DXGI_FORMAT_D24_UNORM_S8_UINT
                               : DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleMask         = UINT_MAX;
    psoDesc.SampleDesc.Count   = 1;

    ComPtr<ID3D12PipelineState> newPso;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&newPso));
    if (FAILED(hr)) {
#ifdef _DEBUG
        fprintf(stderr, "RenderState::GetPSO '%s': PSO creation failed (hr=0x%08X)\n",
                (const char*)shader->m_name, (unsigned)hr);
#endif
        return nullptr;
    }
    ID3D12PipelineState* result = newPso.Get();
    CommandList::GetPsoCache().Insert(key, std::move(newPso));
    return result;
}

// =================================================================================================
