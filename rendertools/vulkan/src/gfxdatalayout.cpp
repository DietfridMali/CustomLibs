#define NOMINMAX

#include "gfxdatalayout.h"
#include "base_shaderhandler.h"
#include "gfxrenderer.h"
#include "commandlist.h"

#include <cstring>

// =================================================================================================
// Vulkan GfxDataLayout implementation
//
// API-neutral parts (FindBuffer / Update*Buffer / Copy / Move / Destroy / Create / SetDynamic /
// SetShape / Activate / Deactivate / IsValid / IsBound / IsActive / static stack helpers /
// ActivateTextures / DeactivateTextures) are unchanged from the DX12 version.
//
// Phase C deferred: Enable / Disable / Render / StartUpdate / FinishUpdate need a live
// VkCommandBuffer (vkCmdBindVertexBuffers / vkCmdBindIndexBuffer / vkCmdDrawIndexed) plus
// CommandList integration. Stubbed below with TODOs.

// Fixed input-slot mapping matching shader.h kSrvBase / kBindingB0 etc. — slot 0..N corresponds
// to VkVertexInputBindingDescription.binding 0..N (and HLSL register-layout positions after
// DXC -> SPIR-V translation).
//   slot 0: Vertex, slot 1-3: TexCoord/0-2, slot 4: Color,
//   slot 5: Normal, slot 6: Tangent, slot 7+: Offset/Float
static int FixedSlotForBuffer(const char* type, int id) noexcept
{
    if (strcmp(type, "Vertex") == 0)
        return 0;
    if (strcmp(type, "TexCoord") == 0) {
        if (id >= 0 and id <= 2)
            return 1 + id;
        return -1;
    }
    if (strcmp(type, "Color") == 0)
        return 4;
    if (strcmp(type, "Normal") == 0)
        return 5;
    if (strcmp(type, "Tangent") == 0)
        return 6;
    if (strcmp(type, "Offset") == 0)
        return 7 + id;
    if (strcmp(type, "Float") == 0)
        return 7 + id;
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
    for (auto& gfxDataBuffer : m_dataBuffers) {
        gfxDataBuffer->Destroy();
        delete gfxDataBuffer;
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
    for (auto gfxDataBuffer : m_dataBuffers) {
        if (gfxDataBuffer->IsType(type) and gfxDataBuffer->HasID(id)) {
            index = i;
            return gfxDataBuffer;
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

// =================================================================================================
// Phase C: Enable / Disable / Render / StartUpdate / FinishUpdate.
// Need a live VkCommandBuffer (current cmd buffer from CommandListHandler — Phase C).

bool GfxDataLayout::Enable(void) noexcept
{
    Activate();
    m_isBound = true;
    // TODO Phase C: bind vertex buffers and index buffer to the active VkCommandBuffer.
    //   VkBuffer    buffers[12];
    //   VkDeviceSize offsets[12] { };
    //   for each m_dataBuffers entry with bufferType==Vertex and IsValid: pack buffers[slot] / offsets[slot]
    //   vkCmdBindVertexBuffers(cb, 0, maxSlot, buffers, offsets);
    //   if m_indexBuffer.IsValid(): vkCmdBindIndexBuffer(cb, m_indexBuffer.Buffer(), 0, m_indexBuffer.IndexType());
    //   Topology is baked into the VkPipeline (no IASetPrimitiveTopology equivalent).
    return true;
}


void GfxDataLayout::Disable(void) noexcept
{
    Deactivate();
    m_isBound = false;
}


CommandList* GfxDataLayout::StartUpdate(void) noexcept
{
    // TODO Phase C: temp command list for buffer-update operations.
    // m_updateList = static_cast<CommandList*>(baseRenderer.StartOperation("GfxDataLayout::Update"));
    m_updateList = nullptr;
    return m_updateList;
}


bool GfxDataLayout::FinishUpdate(void) noexcept
{
    // TODO Phase C: baseRenderer.FinishOperation(m_updateList);
    m_updateList = nullptr;
    return true;
}


void GfxDataLayout::Render(std::span<Texture* const> textures) noexcept
{
    if (not StartRender())
        return;
    ActivateTextures(textures);

    // Flush b1 shader constants (SetFloat/SetVector calls made after Enable()) to GPU.
    Shader* shader = baseShaderHandler.ActiveShader();
    if (shader)
        shader->UpdateVariables();

    // TODO Phase C: vkCmdDrawIndexed / vkCmdDraw via commandListHandler.DrawIndexedInstanced /
    //   DrawInstanced (which currently sit inside the #if 0 Phase-C block in commandlist.h).
    //   Indexed draw count == m_indexBuffer.m_itemCount (uint16/uint32 via VkIndexType).
    //   Non-indexed: sum of m_dataBuffers[0]->m_itemCount.

    DeactivateTextures(textures);
    FinishRender();
}

// =================================================================================================
