// =================================================================================================
// hlslbridge.inl — HLSL → GLSL bridge defines.
//
// Shared shader snippets are written in HLSL syntax as the single source of truth. The OpenGL
// backend prepends this bridge to the final shader source so the same snippet bodies compile as
// GLSL. HLSL backends (DX12, Vulkan via DXC) do NOT include this — for them the HLSL syntax is
// native.
//
// Convention: include the whole bridge, not individual defines. Extra unused defines do no harm,
// and centralizing the mapping in one place keeps the per-shader headers small and consistent.
//
// Provides as a `static const String HLSLBridge` for direct concatenation into the OGL shader
// source assembly (same pattern as the other shared shader-snippet .inl files).
// =================================================================================================

static const String HLSLBridge = String(R"(
    // HLSL → GLSL type aliases
    #define float2   vec2
    #define float3   vec3
    #define float4   vec4
    #define float2x2 mat2
    #define float3x3 mat3
    #define float4x4 mat4
    #define int2     ivec2
    #define int3     ivec3
    #define int4     ivec4
    #define uint2    uvec2
    #define uint3    uvec3
    #define uint4    uvec4

    // HLSL → GLSL builtin renames
    #define lerp        mix
    #define frac        fract
    #define fmod        mod                  // semantics differ for negative operands; safe for non-negative inputs
    #define mul(A, B)   ((A) * (B))
    #define saturate(x) clamp((x), 0.0, 1.0)

    // GLSL has no "static" storage class on globals
    #define static

    // Texture sample wrappers — HLSL calls methods on the texture; GLSL uses free functions
    // and ignores the sampler argument (sampler is bound separately via uniform sampler*D).
    #define SampleLod(tex, samp, uvw, lod) textureLod((tex), (uvw), (lod))
    #define Sample2D(tex, samp, uv)        texture((tex), (uv))
    #define Sample3D(tex, samp, uvw)       texture((tex), (uvw))
)");
