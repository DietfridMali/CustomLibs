#define NOMINMAX

#include "gfxdatalayout.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"
#include "commandlist.h"

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

static DXGI_FORMAT ToIndexFormat(ComponentType componentType) noexcept
{
    return (componentType == ComponentType::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

// Fixed input-slot mapping matching kInputLayout in shader.cpp:
//   slot 0: Vertex, slot 1: TexCoord/0, slot 2: TexCoord/1, slot 3: Color,
//   slot 4: Normal, slot 5: Tangent,    slot 6: TexCoord/2
// Returns -1 for unknown types (caller falls back to insertion order).
static int FixedSlotForBuffer(const char* type, int id) noexcept
{
    if (strcmp(type, "Vertex") == 0)
        return 0;
    if (strcmp(type, "TexCoord") == 0) {
        if (id == 0)
            return 1;
        if (id == 1)
            return 2;
        if (id == 2)
            return 6;
        return -1;
    }
    if (strcmp(type, "Color") == 0)
        return 3;
    if (strcmp(type, "Normal") == 0)
        return 4;
    if (strcmp(type, "Tangent") == 0)
        return 5;
    if (strcmp(type, "Offset") == 0)
        return 5 + id;  // id=0→slot 5 (TANGENT), id=1→slot 6 (TEXCOORD2), id=2→slot 7, id=3→slot 8
    return -1;
}

// =================================================================================================

GfxDataLayout* GfxDataLayout::activeLayout = nullptr;
List<GfxDataLayout*> GfxDataLayout::layoutStack;

// =================================================================================================

bool GfxDataLayout::Create(MeshTopology shape, bool isDynamic) noexcept
{
    m_shape = shape;
    SetDynamic(isDynamic);
    return true;
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
                              buffer.GLDataBuffer(), buffer.GLDataSize(),
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
        if (UpdateIndexBuffer(buffer.GLDataBuffer(), buffer.GLDataSize(), size_t(componentType), forceUpdate))
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
        buffer = new (std::nothrow) GfxDataBuffer();
        if (not buffer)
            return false;
        m_dataBuffers.Append(buffer);
        buffer->SetDynamic(m_isDynamic);
        foundIndex = int(m_dataBuffers.Length()) - 1;
    }
    int slot = FixedSlotForBuffer(type, id);
    if (slot < 0)
        slot = foundIndex;

    return buffer->Update(type, GfxBufferTarget::Vertex, slot, data, dataSize, ComponentType(componentType), componentCount, forceUpdate);
}


bool GfxDataLayout::UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType, bool forceUpdate) noexcept
{
    return m_indexBuffer.Update("Index", GfxBufferTarget::Index, -1, data, dataSize, ComponentType(componentType), 1, forceUpdate);
}


bool GfxDataLayout::Enable(void) noexcept
{
    Activate();
    m_isBound = true;

    auto* list = commandListHandler.CurrentList();
    if (not list) 
        return true;

    // Bind all vertex buffer streams
    int vbCount = m_dataBuffers.Length();
    if (vbCount > 0) {
        // Build views array; use a fixed stack buffer (max 8 streams)
        constexpr int kMaxStreams = 12;
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
        list->IASetVertexBuffers(0, maxSlot, views);
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
    m_updateList = baseRenderer.StartOperation("GfxDataLayout::Update");
    return m_updateList;
}


bool GfxDataLayout::FinishUpdate(void) noexcept {
    bool result = baseRenderer.FinishOperation(m_updateList);
    m_updateList = nullptr;
    return result;
}


void GfxDataLayout::Render(std::span<Texture* const> textures) noexcept
{
#if 0 //def _DEBUG
    fprintf(stderr, "GfxDataLayout::Render on list %p, indexCount=%u, vertCount=%u\n",
        (void*)commandListHandler.CurrentList(),
        m_indexBuffer.IsValid() ? UINT(m_indexBuffer.m_itemCount) : 0,
        (m_dataBuffers.Length() > 0 && m_dataBuffers[0]) ? UINT(m_dataBuffers[0]->m_itemCount) : 0); 
#endif
    if (not StartRender ()) 
        return;

    EnableTextures(textures);

    // Flush b1 shader constants (SetFloat/SetVector calls made after Enable()) to GPU.
    // Enable() uploads b1 first, then the caller sets uniforms — so we must re-upload here.
    if (Shader* shader = baseShaderHandler.ActiveShader())
        shader->UploadB1();

    if (commandListHandler.CurrentList()) {
        if (m_indexBuffer.IsValid() and (m_indexBuffer.m_itemCount > 0))
            commandListHandler.DrawIndexedInstanced(UINT(m_indexBuffer.m_itemCount), 1, 0, 0, 0);
        else {
            // Non-indexed: sum up vertex count from first GfxDataBuffer
            UINT vertCount = 0;
            if (m_dataBuffers.Length() > 0 and m_dataBuffers[0])
                vertCount = UINT(m_dataBuffers[0]->m_itemCount);
            if (vertCount > 0)
                commandListHandler.DrawInstanced(vertCount, 1, 0, 0);
        }
    }

    DisableTextures(textures);
    FinishRender();
}

// =================================================================================================
