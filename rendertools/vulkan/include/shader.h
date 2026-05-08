#pragma once

#include <type_traits>
#include <cstring>
#include <memory>
#include <vector>
#include <span>

#include "vkframework.h"
#include "array.hpp"
#include "vector.hpp"
#include "string.hpp"
#include "gfxstates.h"
#include "shaderdata.h"
#include "shaderdatalayout.h"

// =================================================================================================
// Vulkan Shader
//
// Replaces the DX12 Shader (D3D12 root signature, ID3DBlob bytecode, root CBVs / descriptor
// tables) with:
//  - SPIR-V bytecode per stage (compiled from HLSL via DXC, see shader_compiler.h)
//  - VkShaderModule per stage (m_vsModule / m_fsModule / m_gsModule)
//  - A fixed VkDescriptorSetLayout with bindings:
//      0          UNIFORM_BUFFER_DYNAMIC, ALL_GRAPHICS    (b0  — FrameConstants)
//      1          UNIFORM_BUFFER_DYNAMIC, VERTEX          (b1-VS — ShaderConstants)
//      2          UNIFORM_BUFFER_DYNAMIC, FRAGMENT        (b1-PS — ShaderConstants)
//      3          UNIFORM_BUFFER_DYNAMIC, GEOMETRY        (b1-GS — ShaderConstants)
//      4..19      SAMPLED_IMAGE,         FRAGMENT         (t0..t15)
//      20..35     SAMPLER,               FRAGMENT         (s0..s15) — paired 1:1 with t-slots
//      36..39     STORAGE_IMAGE,         ALL_GRAPHICS     (u0..u3)
//  - VkPipelineLayout from the set layout above (one pipeline layout per shader; cached
//    pipelines built per RenderStates are looked up via the PSO-cache pendant in step 7d).
//  - b0 — FrameConstants written per-draw to a UBO ring-buffer sub-allocation (cbv-allocator
//    pendant, step 10).
//  - b1 — per-stage shader constants, layout reflected from SPIR-V on link.
//  - Same public API as OGL/DX12 Shader (SetFloat, SetInt, SetVector2f, SetMatrix4f,
//    UpdateMatrices…). API-neutral setters write into the existing CPU-side staging buffers.

class Texture;

// -------------------------------------------------------------------------------------------------
// FrameConstants — layout of the b0 constant buffer (256 bytes, register b0 / set 0 binding 0)

#pragma pack(push, 4)
struct FrameConstants {
    float mModelView[16]{};
    float mProjection[16]{};
    float mViewport[16]{};
    float mLightTransform[16]{};
};
static_assert(sizeof(FrameConstants) == 256, "FrameConstants must be 256 bytes");
#pragma pack(pop)

// -------------------------------------------------------------------------------------------------

template <typename T, typename = void>
struct ScalarTraits {
    using scalarType = std::remove_cv_t<T>;
    static constexpr int componentCount = 1;
};
template <typename T>
struct ScalarTraits<T, std::void_t<typename T::value_type>> {
    using scalarType = std::remove_cv_t<typename T::value_type>;
    static constexpr int componentCount = int(sizeof(T) / sizeof(typename T::value_type));
};
template <typename T>
using ScalarBaseType = typename ScalarTraits<std::remove_cv_t<T>>::scalarType;
template <typename T>
inline constexpr int ComponentCount = ScalarTraits<std::remove_cv_t<T>>::componentCount;

// =================================================================================================

class Shader
{
public:
    String  m_name;
    String  m_vs;     // VS HLSL source (for reference / reload)
    String  m_fs;     // PS HLSL source
    String  m_gs;     // GS HLSL source (optional)

    // SPIR-V bytecode per stage (raw bytes; stays alive for VkShaderModule lifetime is not
    // required, but we keep it for reload / debug).
    std::vector<uint8_t> m_vsSpirv;
    std::vector<uint8_t> m_fsSpirv;
    std::vector<uint8_t> m_gsSpirv;

    // Vulkan shader modules (one per stage).
    VkShaderModule  m_vsModule { VK_NULL_HANDLE };
    VkShaderModule  m_fsModule { VK_NULL_HANDLE };
    VkShaderModule  m_gsModule { VK_NULL_HANDLE };

    // Pipeline layout + descriptor set layout (fixed layout per shader).
    VkPipelineLayout       m_pipelineLayout { VK_NULL_HANDLE };
    VkDescriptorSetLayout  m_setLayout      { VK_NULL_HANDLE };

    // b0 — FrameConstants (matrices); written per-draw to a UBO ring-buffer sub-allocation
    FrameConstants  m_b0Staging { };

    // Per-stage shader constants (VS/PS/GS), each uploaded to its own UBO binding (1/2/3)
    struct FieldInfo { uint32_t offset { 0 }; uint32_t size { 0 }; };

    static constexpr int kStageVS = 0;
    static constexpr int kStagePS = 1;
    static constexpr int kStageGS = 2;
    static constexpr int kStageCount = 3;

    // Descriptor-set bindings (Vulkan numeric layout — see header comment above).
    static constexpr uint32_t kBindingB0 = 0;
    static constexpr uint32_t kBindingB1VS = 1;
    static constexpr uint32_t kBindingB1PS = 2;
    static constexpr uint32_t kBindingB1GS = 3;
    static constexpr uint32_t kSrvBase = 4;     // t0..t15 -> bindings 4..19
    static constexpr uint32_t kSrvSlots = 16;
    static constexpr uint32_t kSamplerBase = kSrvBase + kSrvSlots;   // s0..s15 -> bindings 20..35
    static constexpr uint32_t kSamplerSlots = 16;
    static constexpr uint32_t kUavBase = kSamplerBase + kSamplerSlots;  // u0..u3 -> bindings 36..39
    static constexpr uint32_t kUavSlots = 4;
    static constexpr uint32_t kBindingCount = kUavBase + kUavSlots;

    struct StageConstants {
        uint32_t size { 0 };
        std::vector<uint8_t> staging;
        bool dirty { true };
        AutoArray<std::pair<String, FieldInfo>> fields;
    };

    StageConstants m_stages[kStageCount];

    // Dynamic UBO offsets, filled by UploadB0 / UploadB1 from cbvAllocator allocations.
    // Order matches descriptor-set bindings 0..3 (b0, b1-VS, b1-PS, b1-GS). Forwarded to
    // vkCmdBindDescriptorSets via pDynamicOffsets in Shader::UpdateVariables.
    static constexpr uint32_t kDynamicOffsetCount = 4;
    uint32_t  m_dynamicOffsets[kDynamicOffsetCount] { };

    // Per-shader vertex input — built from m_dataLayout on Create(), or via reflection fallback.
    std::vector<VkVertexInputAttributeDescription> m_vsInputAttributes;
    std::vector<VkVertexInputBindingDescription>   m_vsInputBindings;

    // Vertex data layout: describes which C++ buffers feed which shader inputs.
    ShaderDataLayout m_dataLayout;

    // Location table (used for "not found" warning suppression)
    AutoArray<UniformHandle*>  m_uniforms;
    ShaderLocationTable        m_locations;

    using KeyType = String;

    Shader(String name = "", String vs = "", String fs = "", String gs = "")
        : m_name(std::move(name)), m_vs(std::move(vs)), m_fs(std::move(fs)), m_gs(std::move(gs))
    {
        m_uniforms.SetAutoFit(true);
        m_uniforms.SetShrinkable(false);
        m_uniforms.SetDefaultValue(nullptr);
    }

    Shader(const Shader& other) {
        Copy(other);
    }

    Shader(Shader&& other) noexcept {
        Move(other);
    }

    ~Shader() { Destroy(); }

    Shader& operator=(Shader&& other) noexcept {
        return Move(other);
    }

    String& GetKey(void) noexcept {
        return m_name;
    }

    inline VkPipelineLayout GetPipelineLayout(void) const noexcept {
        return m_pipelineLayout;
    }

    inline VkDescriptorSetLayout GetDescriptorSetLayout(void) const noexcept {
        return m_setLayout;
    }

    // -----------------------------------------------------------------------------------------
    // Creation / destruction

    // Compile a single HLSL stage to SPIR-V via DXC. entryPoint: "VSMain" / "PSMain" / "GSMain";
    // target: "vs_6_0" / "ps_6_0" / "gs_6_0".
    bool Compile(const char* hlslCode, const char* entryPoint, const char* target,
                 std::vector<uint8_t>& spirvOut) noexcept;

    // Link: build pipeline layout + descriptor-set layout, build vertex input description from
    // m_dataLayout, reflect b1 fields. gsCode is optional.
    bool Create(const String& vsCode, const String& fsCode, const String& gsCode = "");

    void Destroy(void) noexcept;

    inline bool IsValid(void) const noexcept {
        return (m_vsModule != VK_NULL_HANDLE) and (m_fsModule != VK_NULL_HANDLE);  // GS optional
    }

    // -----------------------------------------------------------------------------------------
    // Runtime

    // Activate this shader: vkCmdBindPipeline via the active CommandList. Pipeline lookup
    // through PipelineCache keyed on {Shader, RenderStates, active RT formats}.
    bool Activate(void);

    // No-op (pipeline changes are driven by RenderStates changes in the next bind).
    inline void Deactivate(void) noexcept {}

    // Upload the b0 / b1 buffers to UBO ring-buffer sub-allocations and stash dynamic offsets
    // for the next vkCmdBindDescriptorSets in UpdateVariables.
    bool UploadB0(void) noexcept;
    bool UploadB1(void) noexcept;

    // Set the 4 standard matrices (mModelView, mProjection, mViewport, mLightTransform).
    // Reads from baseRenderer / shadowMap, same as OGL.
    bool UpdateMatrices(void);

    bool UpdateVariables(void) noexcept;

    // -----------------------------------------------------------------------------------------
    // Pipeline-layout helpers (internal)

    // Build VkDescriptorSetLayout (24 bindings as documented above) and VkPipelineLayout from it.
    bool CreatePipelineLayout(void) noexcept;

    void BuildVertexInput(void) noexcept;

    void UpdateStageFields(const std::vector<uint8_t>& spirv, int stage) noexcept;

    // -----------------------------------------------------------------------------------------
    // Uniform setters — same signatures as DX12 / OGL, return int (was GLint)

private:
    // Write 'size' bytes to all stage buffers that contain the field 'name'.
    // Returns the offset in the first matching stage, or -1 if not found in any stage.
    int SetB1Field(const char* name, const void* data, size_t size) noexcept;

    // Check if 'name' is a b0 field name and write directly to m_b0Staging.
    // Returns true if handled as a b0 field.
    bool TrySetB0Field(const char* name, const float* data) noexcept;

public:
    int SetFloat(const char* name, float data) noexcept;
    int SetInt(const char* name, int data) noexcept;

    int SetVector2f(const char* name, const Vector2f& data) noexcept;
    int SetVector2f(const char* name, Vector2f&& data) noexcept { return SetVector2f(name, static_cast<const Vector2f&>(data)); }
    int SetVector2f(const char* name, float x, float y) noexcept { return SetVector2f(name, Vector2f(x, y)); }

    int SetVector3f(const char* name, const Vector3f& data) noexcept;
    int SetVector3f(const char* name, Vector3f&& data) noexcept { return SetVector3f(name, static_cast<const Vector3f&>(data)); }

    int SetVector4f(const char* name, const Vector4f& data) noexcept;
    int SetVector4f(const char* name, Vector4f&& data) noexcept { return SetVector4f(name, static_cast<const Vector4f&>(data)); }

    int SetVector2i(const char* name, const Vector2i& data) noexcept;
    int SetVector2i(const char* name, Vector2i&& data) noexcept { return SetVector2i(name, static_cast<const Vector2i&>(data)); }

    int SetVector3i(const char* name, const Vector3i& data) noexcept;
    int SetVector3i(const char* name, Vector3i&& data) noexcept { return SetVector3i(name, static_cast<const Vector3i&>(data)); }

    int SetVector4i(const char* name, const Vector4i& data) noexcept;
    int SetVector4i(const char* name, Vector4i&& data) noexcept { return SetVector4i(name, static_cast<const Vector4i&>(data)); }

    int SetMatrix4f(const char* name, const float* data, bool transpose = false) noexcept;
    int SetMatrix4f(const char* name, AutoArray<float>& data, bool transpose = false) noexcept {
        return SetMatrix4f(name, data.Data(), transpose);
    }

    int SetMatrix3f(const char* name, float* data, bool transpose = false) noexcept;
    int SetMatrix3f(const char* name, AutoArray<float>& data, bool transpose = false) noexcept {
        return SetMatrix3f(name, data.Data(), transpose);
    }

    int SetFloatArray(const char* name, const float* data, size_t length) noexcept;
    int SetFloatArray(const char* name, const AutoArray<float>& data) noexcept {
        return SetFloatArray(name, data.Data(), data.Length());
    }

    int SetVector2fArray(const char* name, const Vector2f* data, int length) noexcept;
    int SetVector3fArray(const char* name, const Vector3f* data, int length) noexcept;
    int SetVector4fArray(const char* name, const Vector4f* data, int length) noexcept;

    // -----------------------------------------------------------------------------------------
    // Debug helpers
#ifdef _DEBUG
    static void PrintShaderSource(const char* hlslCode, const char* title) noexcept;
#endif

    // Source-compat stubs (no-ops in Vulkan)
    static void ClearGfxError() noexcept {}

    static bool CheckGfxError(const char* = "") noexcept { return true; }

    // GetFloatData — OGL-specific, kept as no-op stubs
    static inline float* GetFloatData(GLenum /*id*/, int32_t /*size*/, float* data) noexcept { return data; }

    static inline AutoArray<float>& GetFloatData(GLenum /*id*/, int32_t /*size*/, AutoArray<float>& d) noexcept { return d; }

    // -----------------------------------------------------------------------------------------
    // Comparison operators (same as OGL)
    bool operator<(const String& name)  const { return m_name < name; }
    bool operator>(const String& name)  const { return m_name > name; }
    bool operator<=(const String& name) const { return m_name <= name; }
    bool operator>=(const String& name) const { return m_name >= name; }
    bool operator!=(const String& name) const { return m_name != name; }
    bool operator==(const String& name) const { return m_name == name; }
    bool operator<(const Shader& o)  const { return m_name < o.m_name; }
    bool operator>(const Shader& o)  const { return m_name > o.m_name; }
    bool operator<=(const Shader& o) const { return m_name <= o.m_name; }
    bool operator>=(const Shader& o) const { return m_name >= o.m_name; }
    bool operator!=(const Shader& o) const { return m_name != o.m_name; }
    bool operator==(const Shader& o) const { return m_name == o.m_name; }

private:
    Shader& Copy(const Shader& other);
    Shader& Move(Shader& other) noexcept;
};

// =================================================================================================
