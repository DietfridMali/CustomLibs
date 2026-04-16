#define NOMINMAX

#include "gfxdatalayout.h.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"
#include "commandlist.h"

// =================================================================================================
// DX12 gfxdatalayout.h implementation

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

// =================================================================================================

gfxdatalayout.h* gfxdatalayout.h::activeLayout = nullptr;
List<gfxdatalayout.h*> gfxdatalayout.h::layoutStack;

// =================================================================================================

bool gfxdatalayout.h::Create(MeshTopology shape, bool isDynamic) noexcept
{
    m_shape = shape;
    SetDynamic(isDynamic);
    return true;
}


void gfxdatalayout.h::Destroy(void) noexcept
{
    Disable();
    for (auto& GfxDataBuffer : m_dataBuffers) {
        GfxDataBuffer->Destroy();
        delete GfxDataBuffer;
    }
    m_indexBuffer.Destroy();
    m_dataBuffers.Clear();
}


gfxdatalayout.h& gfxdatalayout.h::Copy(gfxdatalayout.h const& other)
{
    if (this != &other) {
        Destroy();
        m_dataBuffers = other.m_dataBuffers;
        m_indexBuffer = other.m_indexBuffer;
        m_shape = other.m_shape;
    }
    return *this;
}


gfxdatalayout.h& gfxdatalayout.h::Move(gfxdatalayout.h& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_dataBuffers = std::move(other.m_dataBuffers);
        m_indexBuffer = std::move(other.m_indexBuffer);
        m_shape = other.m_shape;
    }
    return *this;
}


GfxDataBuffer* gfxdatalayout.h::FindBuffer(const char* type, int id, int& index) noexcept
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


bool gfxdatalayout.h::UpdateDataBuffer(const char* type, int id, BaseVertexDataBuffer& buffer, ComponentType componentType, bool forceUpdate) noexcept
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


void gfxdatalayout.h::UpdateIndexBuffer(IndexBuffer& buffer, ComponentType componentType, bool forceUpdate) noexcept
{
    if (forceUpdate or buffer.IsDirty()) {
        UpdateIndexBuffer(buffer.GLDataBuffer(), buffer.GLDataSize(), size_t(componentType), forceUpdate);
        buffer.SetDirty(false);
    }
}


bool gfxdatalayout.h::UpdateBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate) noexcept
{
    if (strcmp(type, "Index"))
        return UpdateDataBuffer(type, id, data, dataSize, componentType, componentCount, forceUpdate);
    UpdateIndexBuffer(data, dataSize, componentType, forceUpdate);
    return true;
}


bool gfxdatalayout.h::UpdateDataBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate) noexcept
{
    if (dataSize == 0) 
        return false;
    int index = -1;
    GfxDataBuffer* buffer = FindBuffer(type, id, index);
    if (not buffer) {
        buffer = new (std::nothrow) GfxDataBuffer();
        if (not buffer) 
            return false;
        m_dataBuffers.Append(buffer);
        buffer->SetDynamic(m_isDynamic);
        index = m_dataBuffers.Length() - 1;
    }
    return buffer->Update(type, GfxBufferTarget::Vertex, index, data, dataSize, ComponentType(componentType), componentCount, forceUpdate);
}


void gfxdatalayout.h::UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType, bool forceUpdate) noexcept
{
    m_indexBuffer.Update("Index", GfxBufferTarget::Index, -1, data, dataSize, ComponentType(componentType), 1, forceUpdate);
}


bool gfxdatalayout.h::Enable(void) noexcept
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
        constexpr int kMaxStreams = 8;
        D3D12_VERTEX_BUFFER_VIEW views[kMaxStreams]{};
        int bound = 0;
        for (auto GfxDataBuffer : m_dataBuffers) {
            if (bound >= kMaxStreams) 
                break;
            if (GfxDataBuffer and GfxDataBuffer->IsValid() and (GfxDataBuffer->m_bufferType == GfxBufferTarget::Vertex)) {
                views[(GfxDataBuffer->m_index >= 0) ? GfxDataBuffer->m_index : bound] = GfxDataBuffer->m_vbv;
            }
            ++bound;
        }
        list->IASetVertexBuffers(0, bound, views);
    }

    // Bind index buffer if present
    if (m_indexBuffer.IsValid()) 
        list->IASetIndexBuffer(&m_indexBuffer.m_ibv);
    list->IASetPrimitiveTopology(ToD3DTopology(m_shape));
    return true;
}


void gfxdatalayout.h::Disable(void) noexcept
{
    Deactivate();
    m_isBound = false;
}


void gfxdatalayout.h::Render(std::span<Texture* const> textures) noexcept
{
    if (not Enable()) 
        return;

    EnableTextures(textures);

    // Flush b1 shader constants (SetFloat/SetVector calls made after Enable()) to GPU.
    // Enable() uploads b1 first, then the caller sets uniforms — so we must re-upload here.
    if (Shader* sh = baseShaderHandler.ActiveShader())
        sh->UploadB1();

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
    Disable();
}

// =================================================================================================
