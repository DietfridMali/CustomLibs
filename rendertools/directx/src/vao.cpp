#define NOMINMAX

#include "vao.h"
#include "base_shaderhandler.h"
#include "base_renderer.h"
#include "commandlist.h"

// =================================================================================================
// DX12 VAO implementation

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

VAO* VAO::activeVAO = nullptr;
List<VAO*> VAO::vaoStack;

// =================================================================================================

bool VAO::Create(MeshTopology shape, bool isDynamic) noexcept
{
    m_shape = shape;
    SetDynamic(isDynamic);
    return true;
}


void VAO::Destroy(void) noexcept
{
    Disable();
    for (auto& vbo : m_dataBuffers) {
        vbo->Destroy();
        delete vbo;
    }
    m_indexBuffer.Destroy();
    m_dataBuffers.Clear();
}


VAO& VAO::Copy(VAO const& other)
{
    if (this != &other) {
        Destroy();
        m_dataBuffers = other.m_dataBuffers;
        m_indexBuffer = other.m_indexBuffer;
        m_shape = other.m_shape;
    }
    return *this;
}


VAO& VAO::Move(VAO& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_dataBuffers = std::move(other.m_dataBuffers);
        m_indexBuffer = std::move(other.m_indexBuffer);
        m_shape = other.m_shape;
    }
    return *this;
}


VBO* VAO::FindBuffer(const char* type, int id, int& index) noexcept
{
    int i = 0;
    for (auto vbo : m_dataBuffers) {
        if (vbo->IsType(type) and vbo->HasID(id)) { 
            index = i; 
            return vbo; 
        }
        ++i;
    }
    return nullptr;
}


bool VAO::UpdateDataBuffer(const char* type, int id, BaseVertexDataBuffer& buffer, ComponentType componentType, bool forceUpdate) noexcept
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


void VAO::UpdateIndexBuffer(IndexBuffer& buffer, ComponentType componentType, bool forceUpdate) noexcept
{
    if (forceUpdate or buffer.IsDirty()) {
        UpdateIndexBuffer(buffer.GLDataBuffer(), buffer.GLDataSize(), size_t(componentType), forceUpdate);
        buffer.SetDirty(false);
    }
}


bool VAO::UpdateBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate) noexcept
{
    if (strcmp(type, "Index"))
        return UpdateDataBuffer(type, id, data, dataSize, componentType, componentCount, forceUpdate);
    UpdateIndexBuffer(data, dataSize, componentType, forceUpdate);
    return true;
}


bool VAO::UpdateDataBuffer(const char* type, int id, void* data, size_t dataSize, size_t componentType, size_t componentCount, bool forceUpdate) noexcept
{
    if (dataSize == 0) 
        return false;
    int index = -1;
    VBO* vbo = FindBuffer(type, id, index);
    if (not vbo) {
        vbo = new (std::nothrow) VBO();
        if (not vbo) 
            return false;
        m_dataBuffers.Append(vbo);
        vbo->SetDynamic(m_isDynamic);
        index = m_dataBuffers.Length() - 1;
    }
    return vbo->Update(type, GfxBufferTarget::Vertex, index, data, dataSize, ComponentType(componentType), componentCount, forceUpdate);
}


void VAO::UpdateIndexBuffer(void* data, size_t dataSize, size_t componentType, bool forceUpdate) noexcept
{
    m_indexBuffer.Update("Index", GfxBufferTarget::Index, -1, data, dataSize, ComponentType(componentType), 1, forceUpdate);
}


bool VAO::Enable(void) noexcept
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
        for (auto vbo : m_dataBuffers) {
            if (bound >= kMaxStreams) 
                break;
            if (vbo and vbo->IsValid() and (vbo->m_bufferType == GfxBufferTarget::Vertex)) {
                views[(vbo->m_index >= 0) ? vbo->m_index : bound] = vbo->m_vbv;
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


void VAO::Disable(void) noexcept
{
    Deactivate();
    m_isBound = false;
}


void VAO::Render(std::span<Texture* const> textures) noexcept
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
            // Non-indexed: sum up vertex count from first VBO
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
