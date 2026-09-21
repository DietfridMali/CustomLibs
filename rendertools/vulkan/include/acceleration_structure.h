#pragma once

#include "vkframework.h"
#include "gfx_buffer.h"

#include <cstdint>

// =================================================================================================
// AccelerationStructure - a ray tracing acceleration structure (VK_KHR_acceleration_structure).
//
// Two levels, as the API demands them. A BOTTOM level structure is a BVH over triangles and is
// built once per geometry; a TOP level structure is a BVH over INSTANCES, each naming a bottom
// level structure and a 3x4 transform, and is cheap enough to rebuild every frame. A moving
// object changes its instance transform; its bottom level structure is never touched again.
//
// WHY THE GEOMETRY IS COPIED. The build takes device addresses of vertex and index buffers, and
// those addresses must stay valid for as long as the structure is used. A Mesh's index buffer
// does not qualify: GfxDataBuffer rotates a dynamic buffer between FRAME_COUNT slots, and the
// level mesh lays its index buffer out again for every render pass (levelmesh.cpp, BuildBatches).
// So this class allocates buffers of its own and owns them. The cost is the vertex positions a
// second time - three floats per vertex, nothing next to what a level already holds.
//
// Everything here is available only where vkContext.HasRayTracing () is true. The OpenGL and
// DirectX backends carry the same class with the same signatures, where Build... returns false.

struct AccelGeometryDesc
{
    const float*    vertices     { nullptr };  // three floats per vertex, world or model space
    uint32_t        vertexCount  { 0 };
    const uint32_t* indices      { nullptr };  // triangle list
    uint32_t        indexCount   { 0 };
    // An opaque geometry ends a ray at the first triangle it meets. A geometry that is NOT opaque
    // hands every hit back to the shader as a candidate, so an alpha tested texture - a grating -
    // can reject the hit and let the ray carry on.
    bool            opaque       { true };
};


struct AccelInstance
{
    // Row major 3x4 object-to-world transform, the layout VkAccelerationStructureInstanceKHR wants.
    float           transform [12] { 1.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f };
    // Readable in the shader as InstanceID () - what ties a hit back to the caller's own data.
    uint32_t        customIndex  { 0 };
    uint32_t        mask         { 0xFFu };
    // A single sided instance is one a ray's RAY_FLAG_CULL_BACK/FRONT_FACING_TRIANGLES applies to:
    // closed geometry with one winding throughout, where a hit from behind can only mean that the ray
    // started inside it. Everything else is hit from either side whatever the ray asks for - an open
    // model made of single polygons has to cast its shadow in every direction.
    bool            singleSided  { false };
    const class AccelerationStructure* blas { nullptr };
};


namespace RayTracingApi
{
    // Loads the VK_KHR_acceleration_structure entry points and reads the scratch alignment.
    // Called from VKContext::CreateDevice when the device and the shader compiler both support
    // ray query; nothing here works before that.
    bool Load (VkDevice device, VkPhysicalDevice physicalDevice) noexcept;

    bool IsLoaded (void) noexcept;

    uint32_t ScratchAlignment (void) noexcept;
}


// One entry of a batched build: where the structure goes and what it is made of.
struct AccelBuildItem
{
    class AccelerationStructure* structure     { nullptr };
    const AccelGeometryDesc*     geometries    { nullptr };
    uint32_t                     geometryCount { 0 };
};


class AccelerationStructure
{
public:
    // TWO of everything a rebuild touches, used in turn. A top level structure is rebuilt every
    // frame while the frame before it is still tracing against the previous one, so neither the
    // structure, nor its storage, nor the instances it was built from may be written again yet.
    // A bottom level structure is built once and stays in slot 0.
    static constexpr uint32_t   kFrameSlots = 2;

    VkAccelerationStructureKHR  m_handles [kFrameSlots] { VK_NULL_HANDLE, VK_NULL_HANDLE };
    GfxBuffer                   m_storage [kFrameSlots];            // the structures themselves
    VkDeviceSize                m_storageSize [kFrameSlots] { 0, 0 };
    GfxBuffer                   m_instances [kFrameSlots];          // VkAccelerationStructureInstanceKHR[]
    uint32_t                    m_instanceCapacity [kFrameSlots] { 0, 0 };
    // Scratch is reused the same way: the build that reads it is only RECORDED when the call
    // returns, so it must neither be freed nor overwritten until that frame has gone through.
    GfxBuffer                   m_scratch [kFrameSlots];
    VkDeviceSize                m_scratchSize [kFrameSlots] { 0, 0 };
    uint32_t                    m_slot { 0 };

    GfxBuffer                   m_vertices;                     // bottom level: the copied positions
    GfxBuffer                   m_indices;                      // bottom level: the copied indices
    VkDeviceAddress             m_address  { 0 };
    uint32_t                    m_primitives { 0 };

    AccelerationStructure (void) noexcept = default;

    // Owns a VkAccelerationStructureKHR and its buffers, so a copy would free them twice. Moving
    // is allowed - that is what lets a caller keep them in a growing array (one per submodel).
    AccelerationStructure (const AccelerationStructure&) = delete;
    AccelerationStructure& operator= (const AccelerationStructure&) = delete;

    AccelerationStructure (AccelerationStructure&& other) noexcept { Move (other); }

    AccelerationStructure& operator= (AccelerationStructure&& other) noexcept {
        if (this != &other) {
            Destroy ();
            Move (other);
        }
        return *this;
    }

    // Builds MANY bottom level structures with a single submit. Each one still gets its own
    // buffers; what is shared is the scratch allocation and the wait for the GPU, and that wait
    // is what a per structure build pays for over and over - half a millisecond each, which a
    // level's worth of models turns into a visible pause.
    static bool BuildBottomLevelBatch (const AccelBuildItem* items, uint32_t itemCount) noexcept;

    bool BuildBottomLevel (const AccelGeometryDesc* geometries, uint32_t geometryCount) noexcept;

    // Rebuilds the top level structure from scratch. The instance buffer is reused while it is
    // big enough, so a per frame rebuild allocates nothing.
    bool BuildTopLevel (const AccelInstance* instances, uint32_t instanceCount, bool immediate = false) noexcept;

    // Puts the structure into the bind table for shaders that declare a
    // RaytracingAccelerationStructure on register (t0, space2). To be called after BuildTopLevel ()
    // of the same frame - the handle changes with the frame slot.
    bool Bind (void) const noexcept;

    void Destroy (void) noexcept;

    inline VkAccelerationStructureKHR Handle (void) const noexcept { return m_handles [m_slot]; }
    inline VkDeviceAddress DeviceAddress (void) const noexcept { return m_address; }
    inline uint32_t Primitives (void) const noexcept { return m_primitives; }
    inline bool IsValid (void) const noexcept { return m_handles [m_slot] != VK_NULL_HANDLE; }

private:
    void Move (AccelerationStructure& other) noexcept;
};

// =================================================================================================
