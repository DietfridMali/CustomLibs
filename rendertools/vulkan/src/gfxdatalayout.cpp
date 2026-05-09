#define NOMINMAX

#include "gfxdatalayout.h"
#include "base_shaderhandler.h"
#include "gfxrenderer.h"
#include "commandlist.h"
#include "gfxstates.h"

#include <cstring>

// =================================================================================================
// Vulkan GfxDataLayout implementation
//
// API-neutral parts (FindBuffer / Update*Buffer / Copy / Move / Destroy / Create / SetDynamic /
// SetShape / Activate / Deactivate / IsValid / IsBound / IsActive / static stack helpers /
// ActivateTextures / DeactivateTextures) are unchanged from the DX12 version.
//
// Enable / Disable / Render / StartUpdate / FinishUpdate drive the active VkCommandBuffer:
// vkCmdBindVertexBuffers / vkCmdBindIndexBuffer / vkCmdDrawIndexed via the CommandListHandler
// wrappers, with the temp-CL routing through baseRenderer.StartOperation / FinishOperation.

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
// Enable / Disable / Render / StartUpdate / FinishUpdate — Vulkan implementation.
//
// 1:1 port of the DX12 path. IASetVertexBuffers -> vkCmdBindVertexBuffers,
// IASetIndexBuffer -> vkCmdBindIndexBuffer, IASetPrimitiveTopology -> baked into the pipeline.
// DrawInstanced / DrawIndexedInstanced go through the matching commandListHandler wrappers.

bool GfxDataLayout::Enable(void) noexcept
{
    Activate();
    m_isBound = true;

    VkCommandBuffer cb = commandListHandler.CurrentGfxList();
    if (cb == VK_NULL_HANDLE)
        return true;

    constexpr int kMaxStreams = 12;
    VkBuffer     buffers[kMaxStreams] { };
    VkDeviceSize offsets[kMaxStreams] { };
    int maxSlot = 0;
    for (auto* gdb : m_dataBuffers) {
        if (not gdb or not gdb->IsValid() or (gdb->m_bufferType != GfxBufferTarget::Vertex))
            continue;
        int slot = (gdb->m_index >= 0) ? gdb->m_index : maxSlot;
        if (slot >= kMaxStreams)
            continue;
        buffers[slot] = gdb->Buffer();
        offsets[slot] = 0;
        if (slot >= maxSlot)
            maxSlot = slot + 1;
    }
    if (maxSlot > 0)
        vkCmdBindVertexBuffers(cb, 0, uint32_t(maxSlot), buffers, offsets);

    if (m_indexBuffer.IsValid())
        vkCmdBindIndexBuffer(cb, m_indexBuffer.Buffer(), 0, m_indexBuffer.IndexType());

    // Primitive topology is baked into the VkPipeline (no IASetPrimitiveTopology equivalent
    // in dynamic rendering). m_shape feeds the PipelineKey via baseRenderer.RenderStates().
    return true;
}


void GfxDataLayout::Disable(void) noexcept
{
    Deactivate();
    m_isBound = false;
}


CommandList* GfxDataLayout::StartUpdate(void) noexcept
{
    m_updateList = static_cast<CommandList*>(baseRenderer.StartOperation("GfxDataLayout::Update"));
    return m_updateList;
}


bool GfxDataLayout::FinishUpdate(void) noexcept
{
    bool result = baseRenderer.FinishOperation(m_updateList);
    m_updateList = nullptr;
    return result;
}


void GfxDataLayout::Render(std::span<Texture* const> textures) noexcept
{
    if (not StartRender())
        return;
    ActivateTextures(textures);
    gfxStates.CheckError();

    // Flush b1 shader constants (SetFloat/SetVector calls made after Enable()) to GPU and
    // materialize the bind table into a VkDescriptorSet for this draw.
    Shader* shader = baseShaderHandler.ActiveShader();
    if (shader)
        shader->UpdateVariables();
    gfxStates.CheckError();
    if (commandListHandler.CurrentGfxList() != VK_NULL_HANDLE) {
        if (m_indexBuffer.IsValid() and (m_indexBuffer.m_itemCount > 0))
            commandListHandler.DrawIndexedInstanced(uint32_t(m_indexBuffer.m_itemCount), 1, 0, 0, 0);
        else {
            uint32_t vertCount = 0;
            if (m_dataBuffers.Length() > 0 and m_dataBuffers[0])
                vertCount = uint32_t(m_dataBuffers[0]->m_itemCount);
            if (vertCount > 0)
                commandListHandler.DrawInstanced(vertCount, 1, 0, 0);
        }
    }
    gfxStates.CheckError();
    DeactivateTextures(textures);
    gfxStates.CheckError();
    FinishRender();
    gfxStates.CheckError();
}

// =================================================================================================
