#pragma once

#include <type_traits>
#include <cstring>
#include <memory>
#include <vector>
#include <span>

#include "framework.h"
#include "array.hpp"
#include "vector.hpp"
#include "string.hpp"
#include "gfxdriverstates.h"
#include "shaderdata.h"
#include "shaderdatalayout.h"

// =================================================================================================
// DX12 Shader
//
// Replaces the OGL Shader (glCreateProgram / glUniform*) with:
//  - Compiled HLSL blobs (ID3DBlob via D3DCompile)
//  - A fixed root signature: root CBV b0 (FrameConstants), root CBV b1 (ShaderConstants),
//    SRV descriptor table t0..t15 (pixel shader)
//  - PSO cache keyed on RenderState (created on demand in Enable())
//  - b0: 4 x 4x4 matrices (mModelView, mProjection, mViewport, mLightTransform)
//  - b1: per-shader constants, layout from HLSL reflection on link
//  - Same public API as OGL Shader (SetFloat, SetInt, SetVector2f, SetMatrix4f, UpdateMatrices …)

// Forward declaration
class Texture;

// -------------------------------------------------------------------------------------------------
// FrameConstants — layout of the b0 constant buffer (256 bytes, register b0)

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
    String  m_vs;     // VS source (for reference / reload)
    String  m_fs;     // PS source (for reference / reload)
    String  m_gs;     // GS source (optional)

    // HLSL bytecodes
    ComPtr<ID3DBlob>  m_vsBlob;
    ComPtr<ID3DBlob>  m_psBlob;
    ComPtr<ID3DBlob>  m_gsBlob;  // optional

    // Shared root signature (fixed layout, created once per shader)
    ComPtr<ID3D12RootSignature> m_rootSignature;

    // PSO cache: one entry per unique RenderState
    struct PsoEntry {
        RenderState               state;
        ComPtr<ID3D12PipelineState> pso;
    };
    AutoArray<PsoEntry>  m_psoCache;

    // b0 — FrameConstants (matrices); written per-draw to a cbvAllocator sub-allocation
    FrameConstants          m_b0Staging{};

    // b1 — per-shader constants; written per-draw to a cbvAllocator sub-allocation
    struct FieldInfo { uint32_t offset{ 0 }; uint32_t size{ 0 }; };
    uint32_t                     m_b1Size{ 0 };
    std::vector<uint8_t>         m_b1Staging;
    bool                         m_b1Dirty{ true };

    // Field map (uniform name → b1 byte offset + size), filled from HLSL reflection
    AutoArray<std::pair<String, FieldInfo>> m_b1Fields;

    // Per-shader input layout — built from m_dataLayout on Create(), or via reflection fallback.
    std::vector<D3D12_INPUT_ELEMENT_DESC> m_vsInputLayout;

    // Vertex data layout: describes which C++ buffers feed which shader inputs.
    ShaderDataLayout m_dataLayout;

    // Location table (name → resolved b1 offset, same pattern as OGL for compat)
    AutoArray<UniformHandle*>  m_uniforms;
    ShaderLocationTable        m_locations;

    // Cached PSO for current RenderState (reset in Enable when state changes)
    ID3D12PipelineState* m_activePso{ nullptr };

    using KeyType = String;

    Shader(String name = "", String vs = "", String fs = "", String gs = "")
        : m_name(std::move(name)), m_vs(std::move(vs)), m_fs(std::move(fs)), m_gs(std::move(gs))
    {
        m_uniforms.SetAutoFit(true);
        m_uniforms.SetShrinkable(false);
        m_uniforms.SetDefaultValue(nullptr);
    }

    Shader(const Shader& other)  { 
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

    // -----------------------------------------------------------------------------------------
    // Creation / destruction

    // Compile a single HLSL stage.  entryPoint: "VSMain" or "PSMain"; target: "vs_5_1"/"ps_5_1"
    bool Compile(const char* hlslCode, const char* entryPoint, const char* target,
                 ComPtr<ID3DBlob>& blobOut) noexcept;

    // Link: build root signature, build input layout from m_dataLayout (or reflection fallback),
    // reflect b1 fields. gsCode is optional.
    bool Create(const String& vsCode, const String& fsCode, const String& gsCode = "");

    void Destroy(void) noexcept;

    inline bool IsValid(void) const noexcept { return m_vsBlob && m_psBlob; }  // GS is optional

    // -----------------------------------------------------------------------------------------
    // Runtime

    // Activate this shader: select / create PSO, bind root sig, upload CBs, bind descriptor table.
    void Enable(void);

    // No-op in DX12 (PSO changes are driven by RenderState changes in the next Enable call).
    inline void Disable(void) noexcept {}

    // Allocate a b1 slot from cbvAllocator, write m_b1Staging, bind root param 1.
    // Called from GfxDataLayout::Render() just before each draw.
    bool UploadB1(void) noexcept;

    // Set the 4 standard matrices (mModelView, mProjection, mViewport, mLightTransform).
    // Reads from baseRenderer / shadowMap, same as OGL.
    bool UpdateMatrices(void);

    // -----------------------------------------------------------------------------------------
    // PSO helpers (internal)

    bool CreateRootSignature(void) noexcept;

    ID3D12PipelineState* GetOrCreatePSO(const RenderState& state) noexcept;

    static D3D12_BLEND ToD3DBlend(GLenum gl) noexcept;

    static D3D12_BLEND_OP ToD3DBlendOp(GLenum gl) noexcept;

    static D3D12_COMPARISON_FUNC ToD3DCompFunc(GLenum gl) noexcept;

    static D3D12_STENCIL_OP ToD3DStencilOp(GLenum gl) noexcept;

    // -----------------------------------------------------------------------------------------
    // Uniform setters — same signatures as OGL, return int (was GLint)

private:
    // Write 'size' bytes at 'data' to the b1 staging buffer at the offset for 'name'.
    // Resolves the offset on first call (via m_b1Fields), caches it in m_locations.
    // Returns the b1 byte offset, or -1 if the name is not found.
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

    // Source-compat stubs (no-ops in DX12)
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
