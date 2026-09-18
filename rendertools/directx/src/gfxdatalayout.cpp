#define NOMINMAX

#include "gfxdatalayout.h"
#include "mesh.h"
#include "base_shaderhandler.h"
#include "gfxrenderer.h"
#include "commandlist.h"
#include "tracy_wrapper.h"

#include <cassert>
#include <algorithm>
#include <new>
#include <vector>

extern bool ResolveDrawPipeline(CommandList* cl, Shader* shader) noexcept;

// =================================================================================================
// DX12 GfxDataLayout implementation

static D3D_PRIMITIVE_TOPOLOGY ToD3DTopology(MeshTopology topology) noexcept
{
    switch (topology) {
        case MeshTopology::Triangles: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case MeshTopology::Lines:     return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case MeshTopology::Points:    return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case MeshTopology::Quads:     return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; // quads → triangles in mesh.cpp
        default:                      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

static D3D_PRIMITIVE_TOPOLOGY ToD3DPatchTopology(MeshTopology topology) noexcept
{
    switch (topology) {
        case MeshTopology::Lines:     return D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST;
        case MeshTopology::Points:    return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        default:                      return D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
    }
}

static DXGI_FORMAT ToIndexFormat(ComponentType componentType) noexcept
{
    return (componentType == ComponentType::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

// Input slots come from the central vertex attribute registry (GfxAttributeSlot in
// shaderdatalayout.h), matching the D3D12_INPUT_ELEMENT_DESC slots built in shader.cpp.

// =================================================================================================

GfxDataLayout* GfxDataLayout::activeLayout = nullptr;
List<GfxDataLayout*> GfxDataLayout::layoutStack;

// =================================================================================================

bool GfxDataLayout::Create(MeshTopology shape, uint32_t dynamicBuffers) noexcept
{
    m_shape = shape;
    SetDynamic(dynamicBuffers);
    return true;
}


void GfxDataLayout::SetDynamic(uint32_t dynamicBuffers) noexcept
{
    m_dynamicBuffers = dynamicBuffers;
    for (auto gfxDataBuffer : m_dataBuffers)
        gfxDataBuffer->SetDynamic((dynamicBuffers & Mesh::MeshBufferBit(gfxDataBuffer->m_type, gfxDataBuffer->m_id)) != 0);
    m_indexBuffer.SetDynamic((dynamicBuffers & Mesh::mbIndex) != 0);
}


void GfxDataLayout::Destroy(void) noexcept
{
    Disable();
    for (auto& GfxDataBuffer : m_dataBuffers) {
        GfxDataBuffer->Destroy();
        delete GfxDataBuffer;
    }
    m_indexBuffer.Destroy();
    m_dataBuffers.Clear();
}


GfxDataLayout& GfxDataLayout::Copy(GfxDataLayout const& other)
{
    if (this != &other) {
        Destroy();
        m_dataBuffers = other.m_dataBuffers;
        m_indexBuffer = other.m_indexBuffer;
        m_shape = other.m_shape;
        m_instanceCount = other.m_instanceCount;
    }
    return *this;
}


GfxDataLayout& GfxDataLayout::Move(GfxDataLayout& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_dataBuffers = std::move(other.m_dataBuffers);
        m_indexBuffer = std::move(other.m_indexBuffer);
        m_shape = other.m_shape;
        m_instanceCount = other.m_instanceCount;
    }
    return *this;
}


GfxDataBuffer* GfxDataLayout::FindBuffer(const char* type, int id, int& index) noexcept
{
    int i = 0;
    for (auto GfxDataBuffer : m_dataBuffers) {
        if (GfxDataBuffer->IsType(type) and GfxDataBuffer->HasID(id)) { 
            index = i; 
            return GfxDataBuffer; 
        }
        ++i;
    }
    return nullptr;
}


bool GfxDataLayout::UpdateDataBuffer(const char* type, int id, BaseVertexDataBuffer& buffer, ComponentType componentType, bool forceUpdate) noexcept
{
    if (forceUpdate or buffer.IsDirty()) {
        if (not UpdateDataBuffer(type, id,
                              buffer.GfxDataBuffer(), buffer.GfxDataSize(),
                              size_t(componentType),
                              size_t(buffer.ComponentCount()), forceUpdate))
            return false;
        buffer.SetDirty(false);
    }
    return true;
}


void GfxDataLayout::UpdateIndexBuffer(IndexBuffer& buffer, ComponentType componentType, bool forceUpdate) noexcept
{
    if (forceUpdate or buffer.IsDirty()) {
        if (UpdateIndexBuffer(buffer.GfxDataBuffer(), buffer.GfxDataSize(), size_t(componentType), forceUpdate))
            buffer.SetDirty(false);
    }
}


bool GfxDataLayout::UpdateBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate) noexcept
{
    if (strcmp(type, "Index"))
        return UpdateDataBuffer(type, id, data, dataSize, componentType, componentCount, forceUpdate);
    return UpdateIndexBuffer(data, dataSize, componentType, forceUpdate);
}


bool GfxDataLayout::UpdateDataBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate) noexcept
{
    if (dataSize == 0)
        return false;
    int foundIndex = -1;
    GfxDataBuffer* buffer = FindBuffer(type, id, foundIndex);
    if (not buffer) {
        buffer = new (std::nothrow) GfxDataBuffer(type, id);
        if (not buffer)
            return false;
        m_dataBuffers.Append(buffer);
        buffer->SetDynamic((m_dynamicBuffers & Mesh::MeshBufferBit(type, id)) != 0);
        foundIndex = int(m_dataBuffers.Length()) - 1;
    }
    int slot = GfxAttributeSlot(type, id);
    assert(slot >= 0);  // unknown buffer tags are not part of the attribute registry
    if (slot < 0)
        return false;

    return buffer->Update(type, GfxBufferTarget::Vertex, slot, data, dataSize, ComponentType(componentType), componentCount, forceUpdate);
}


bool GfxDataLayout::UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType, bool forceUpdate) noexcept
{
    return m_indexBuffer.Update("Index", GfxBufferTarget::Index, -1, data, dataSize, ComponentType(componentType), 1, forceUpdate);
}

// =================================================================================================

struct DefaultVertexStreams {
    GfxDataBuffer   zeros{ "DefaultVertexZeros", 0, GfxBufferTarget::Vertex, true };
    GfxDataBuffer   unitW{ "DefaultVertexUnitW", 0, GfxBufferTarget::Vertex, true };
    uint32_t        capacity{ 0 };
};

static DefaultVertexStreams* defaultStreams = nullptr;


static size_t AttributeStride(ShaderDataAttributes::Format format) noexcept
{
    switch (format) {
        case ShaderDataAttributes::Float1:
        case ShaderDataAttributes::Uint1:
            return 4;
        case ShaderDataAttributes::Float2:
        case ShaderDataAttributes::Uint2:
            return 8;
        case ShaderDataAttributes::Float3:
        case ShaderDataAttributes::Uint3:
            return 12;
        default:
            return 16;
    }
}


static const D3D12_VERTEX_BUFFER_VIEW* DefaultVertexView(uint32_t vertexCount, ShaderDataAttributes::Format format) noexcept
{
    if (not defaultStreams) {
        defaultStreams = new (std::nothrow) DefaultVertexStreams;
        if (not defaultStreams)
            return nullptr;
    }
    DefaultVertexStreams& streams = *defaultStreams;
    if (vertexCount > streams.capacity) {
        uint32_t capacity = std::max(std::max(vertexCount, streams.capacity * 2), uint32_t(4096));
        std::vector<float> data(size_t(capacity) * 4, 0.0f);
        if (not streams.zeros.Update("DefaultVertexZeros", GfxBufferTarget::Vertex, 0, data.data(), data.size() * sizeof(float), ComponentType::Float, 4))
            return nullptr;
        for (size_t i = 3; i < data.size(); i += 4)
            data[i] = 1.0f;
        if (not streams.unitW.Update("DefaultVertexUnitW", GfxBufferTarget::Vertex, 0, data.data(), data.size() * sizeof(float), ComponentType::Float, 4))
            return nullptr;
        streams.capacity = capacity;
    }
    return (format == ShaderDataAttributes::Float4) ? &streams.unitW.m_vbv : &streams.zeros.m_vbv;
}


void GfxDataLayout::DestroyDefaultStreams(void) noexcept
{
    if (not defaultStreams)
        return;
    defaultStreams->zeros.Destroy();
    defaultStreams->unitW.Destroy();
    delete defaultStreams;
    defaultStreams = nullptr;
}


bool GfxDataLayout::Enable(void) noexcept
{
    ZoneScopedN("Layout::Enable");
    Activate();
    m_isBound = true;

    auto* list = commandListHandler.CurrentGfxList();
    if (not list) 
        return true;

    // Bind all vertex buffer streams
    int vbCount = m_dataBuffers.Length();
    if (vbCount > 0) {
        // Build views array; fixed stack buffer covering all registry slots (0-15)
        constexpr int kMaxStreams = 16;
        D3D12_VERTEX_BUFFER_VIEW views[kMaxStreams]{};
        int maxSlot = 0;
        for (auto GfxDataBuffer : m_dataBuffers) {
            if (not GfxDataBuffer or not GfxDataBuffer->IsValid() or (GfxDataBuffer->m_bufferType != GfxBufferTarget::Vertex))
                continue;
            int slot = (GfxDataBuffer->m_index >= 0) ? GfxDataBuffer->m_index : maxSlot;
            if (slot < kMaxStreams) {
                views[slot] = GfxDataBuffer->m_vbv;
                if (slot >= maxSlot)
                    maxSlot = slot + 1;
            }
        }
        Shader* shader = baseShaderHandler.ActiveShader();
        if (shader) {
            uint32_t vertexCount = 0;
            for (auto gdb : m_dataBuffers) {
                if (gdb and gdb->IsValid() and (gdb->m_bufferType == GfxBufferTarget::Vertex))
                    vertexCount = std::max(vertexCount, gdb->m_itemCount);
            }
            const ShaderDataLayout& layout = shader->m_dataLayout;
            for (int i = 0; i < layout.m_count; ++i) {
                int slot = GfxAttributeSlot(layout.m_attrs[i].datatype, layout.m_attrs[i].id);
                if ((slot < 0) or (slot >= kMaxStreams) or (views[slot].BufferLocation != 0))
                    continue;
                const D3D12_VERTEX_BUFFER_VIEW* pView = DefaultVertexView(vertexCount, layout.m_attrs[i].format);
                if (not pView)
                    continue;
                views[slot] = *pView;
                views[slot].StrideInBytes = UINT(AttributeStride(layout.m_attrs[i].format));
                if (slot >= maxSlot)
                    maxSlot = slot + 1;
            }
        }
        list->IASetVertexBuffers(0, maxSlot, views);
#if 0//def _DEBUG
        fprintf(stderr, "GfxDataLayout::Enable — %d buffers, maxSlot=%d\n", vbCount, maxSlot);
        for (int s = 0; s < maxSlot; ++s) {
            if (views[s].BufferLocation)
                fprintf(stderr, "  slot %d: addr=0x%llx stride=%u size=%u\n",
                    s, views[s].BufferLocation, views[s].StrideInBytes, views[s].SizeInBytes);
            else
                fprintf(stderr, "  slot %d: <empty>\n", s);
        }
#endif
    }

    // Bind index buffer if present
    if (m_indexBuffer.IsValid())
        list->IASetIndexBuffer(&m_indexBuffer.m_ibv);
    list->IASetPrimitiveTopology(ToD3DTopology(m_shape));
    return true;
}


void GfxDataLayout::Disable(void) noexcept
{
    Deactivate();
    m_isBound = false;
}



CommandList* GfxDataLayout::StartUpdate(void) noexcept {
    m_updateList = static_cast<CommandList*>(baseRenderer.StartOperation("GfxDataLayout::Update", false));
    return m_updateList;
}


bool GfxDataLayout::FinishUpdate(void) noexcept {
    bool result = baseRenderer.FinishOperation(m_updateList, true);
    m_updateList = nullptr;
    return result;
}


void GfxDataLayout::Render(std::span<Texture* const> textures, uint32_t firstIndex, uint32_t indexCount) noexcept
{
#if 0 //def _DEBUG
    fprintf(stderr, "GfxDataLayout::Render on list %p, indexCount=%u, vertCount=%u\n",
        (void*)commandListHandler.CurrentGfxList(),
        m_indexBuffer.IsValid() ? UINT(m_indexBuffer.m_itemCount) : 0,
        (m_dataBuffers.Length() > 0 && m_dataBuffers[0]) ? UINT(m_dataBuffers[0]->m_itemCount) : 0); 
#endif
    if (not StartRender())
        return;
    {
        ZoneScopedN("Layout::ActivateTextures");
        ActivateTextures(textures);
    }

    // Flush b1 shader constants (SetFloat/SetVector calls made after Enable()) to GPU.
    // Enable() uploads b1 first, then the caller sets uniforms — so we must re-upload here.
    Shader* shader = baseShaderHandler.ActiveShader();
    // A draw whose constants could not be set up is not recorded: it would run with the constant
    // buffers of whatever draw came before it.
    bool hasVariables = true;
    if (shader) {
        if (CommandList* cl = commandListHandler.CurrentCmdList()) {
            cl->SetTopology(shader, m_shape);
            ResolveDrawPipeline(cl, shader);
        }
        ZoneScopedN("Shader::UpdateVariables");
        hasVariables = shader->UpdateVariables();
    }

    {
        ZoneScopedN("Layout::DrawCall");
        if (hasVariables and commandListHandler.CurrentGfxList()) {
            commandListHandler.CurrentGfxList()->IASetPrimitiveTopology((shader and shader->IsTessellated()) ? ToD3DPatchTopology(m_shape) : ToD3DTopology(m_shape));
            if (m_indexBuffer.IsValid() and (m_indexBuffer.m_itemCount > 0)) {
                UINT count = (indexCount > 0) ? UINT(indexCount) : UINT(m_indexBuffer.m_itemCount) - UINT(firstIndex);
                if (count > 0) {
                    commandListHandler.DrawIndexedInstanced(count, m_instanceCount, UINT(firstIndex), 0, 0);
                    gfxStates.CountDraw();
                }
            }
            else {
                // Non-indexed: sum up vertex count from first GfxDataBuffer
                UINT vertCount = 0;
                if (m_dataBuffers.Length() > 0 and m_dataBuffers[0])
                    vertCount = UINT(m_dataBuffers[0]->m_itemCount);
                if (vertCount > 0) {
                    commandListHandler.DrawInstanced(vertCount, m_instanceCount, 0, 0);
                    gfxStates.CountDraw();
                }
            }
        }
    }
    {
        ZoneScopedN("Layout::Finish");
        // The textures stay bound. GfxStates::BindTexture () returns at once when the same texture is
            // already on the same unit, so a batch that keeps using them costs nothing after the first draw -
            // releasing them here threw that away and made every draw bind again, plus two calls for the
            // release itself. Nor did it protect anything: a slot a later draw does not assign is a fault in
            // that draw's shader setup, and one that is better seen than papered over.
        FinishRender();
    }
    {
        ZoneScopedN("Layout::CheckError");
        gfxStates.CheckError();
    }
}

// =================================================================================================
