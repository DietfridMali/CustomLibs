
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// HLSL shader strings for the DirectX 12 backend — blur / AA post-process shaders.

static const ShaderDataAttributes VtxTcAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};
//
// All three shaders use Offset2DVS() for the VS (which declares b1 with 'float offset').
// The PS b1 starts with 'float vsOffset' at the same slot to keep the combined cbuffer layout
// consistent across VS and PS.
// =================================================================================================


// -------------------------------------------------------------------------------------------------
// Box/Gauss blur wrapper: runs GaussBlur(uv, 3, 1) — fixed 7×7 kernel with spread 1.
// ShaderConstants: vsOffset (= VS offset), texelSize, blurStrength, blurSpread.
const ShaderSource& BoxBlurShader() {
    static const ShaderSource boxBlurShader(
        "boxblur",
        Offset2DVS(),
        String(R"(
            cbuffer ShaderConstants : register(b1) {
                float  vsOffset;      // VS 'offset' lives at byte 0; PS ignores it
                float2 texelSize;     // used by GaussBlurFuncs
                int    blurStrength;  // used by GaussBlurFuncs (ignored — hardcoded 3 below)
                float  blurSpread;    // used by GaussBlurFuncs (ignored — hardcoded 1 below)
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
        )") +
        GaussBlurFuncs() +
        String(R"(
            float4 PSMain(PSInput i) : SV_Target {
                return GaussBlur(i.fragCoord, 3, 1);
            }
        )"),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return boxBlurShader;
}


// -------------------------------------------------------------------------------------------------
// FXAA (Fast Approximate Anti-Aliasing).
// textureSize() replaced by texelSize uniform.
// textureOffset → SampleLevel with int2 offset; textureLod → SampleLevel.
// ShaderConstants: vsOffset, texelSize.
const ShaderSource& FxaaShader() {
    static const ShaderSource fxaaShader(
        "fxaa",
        Offset2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float  vsOffset;   // VS 'offset'; PS ignores
                float  _pad0;
                float2 texelSize;
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            static const float FXAA_SPAN_MAX   = 16.0;
            static const float FXAA_REDUCE_MIN = 1.0 / 128.0;
            static const float FXAA_REDUCE_MUL = 1.0 / 8.0;
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float3 FxaaPixelShader(float2 pos, float2 rcpFrame) {
                float3 rgbNW = surface.SampleLevel(s0, pos, 0, int2(-1,-1)).xyz;
                float3 rgbNE = surface.SampleLevel(s0, pos, 0, int2( 1,-1)).xyz;
                float3 rgbSW = surface.SampleLevel(s0, pos, 0, int2(-1, 1)).xyz;
                float3 rgbSE = surface.SampleLevel(s0, pos, 0, int2( 1, 1)).xyz;
                float3 rgbM  = surface.SampleLevel(s0, pos, 0).xyz;
                const float3 luma = float3(0.299, 0.587, 0.114);
                float lumaNW = dot(rgbNW, luma);
                float lumaNE = dot(rgbNE, luma);
                float lumaSW = dot(rgbSW, luma);
                float lumaSE = dot(rgbSE, luma);
                float lumaM  = dot(rgbM,  luma);
                float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
                float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
                float2 dir = float2(
                    -((lumaNW + lumaNE) - (lumaSW + lumaSE)),
                     ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
                float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL),
                                      FXAA_REDUCE_MIN);
                float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
                dir = clamp(dir * rcpDirMin, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * rcpFrame;
                float3 rgbA = 0.5 * (surface.SampleLevel(s0, pos + dir * (1.0/3.0 - 0.5), 0).xyz
                                   + surface.SampleLevel(s0, pos + dir * (2.0/3.0 - 0.5), 0).xyz);
                float3 rgbB = rgbA * 0.5 + 0.25 * (surface.SampleLevel(s0, pos + dir * -0.5, 0).xyz
                                                  + surface.SampleLevel(s0, pos + dir *  0.5, 0).xyz);
                float lumaB = dot(rgbB, luma);
                return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
            }
            float4 PSMain(PSInput i) : SV_Target {
                float3 color = FxaaPixelShader(i.fragCoord, texelSize);
                float  a     = surface.Sample(s0, i.fragCoord).a;
                return float4(color, a);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return fxaaShader;
}


// -------------------------------------------------------------------------------------------------
// Separable Gaussian blur with per-shader kernel (up to 33 taps).
// HLSL pads scalar arrays to 16-byte slots; the C++ SetFloatArray setter emits padded uploads.
// ShaderConstants: vsOffset, direction (0=horiz,1=vert), radius (half-kernel size),
//                  texelSize, coeffs[33].
const ShaderSource& GaussBlurShader() {
    static const ShaderSource gaussBlurShader(
        "gaussblur",
        Offset2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float  vsOffset;          // VS 'offset'; PS ignores
                float  direction;         // 0=horizontal, 1=vertical
                int    radius;            // half-kernel (n = 2*radius+1 taps, max 16)
                float  _pad0;
                float2 texelSize;
                float2 _pad1;
                float  coeffs[33];
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float2 scrollDir = float2(1.0 - direction, direction);
                float4 sum = (float4)0;
                int n = 2 * radius + 1;
                for (int k = 0; k < n; ++k) {
                    float2 coord = i.fragCoord + scrollDir * float(k - radius) * texelSize;
                    sum += surface.Sample(s0, coord) * coeffs[k];
                }
                return float4(sum.rgb, sum.a);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return gaussBlurShader;
}

// =================================================================================================

// -------------------------------------------------------------------------------------------------
// The bilateral (edge stopping) blur - see the note in the OpenGL source. distanceSource picks where
// the surface distance comes from: 0 the world position G-buffer (plane distance), 1 the scene depth
// (linearized through projDepth). Everything else is shared between the two users.

const ShaderSource& BilateralBlurShader() {
    static const ShaderSource source(
        "bilateralBlur",
        Standard2DVS(),
        String(R"(
            cbuffer ShaderConstants : register(b1) {
                float2 texelSize;
                float  direction;
                int    radius;
                float  normalPower;
                float  posSigma;
                int    distanceSource;
                float2 projDepth;
            };
            Texture2D    surface         : register(t0);
            Texture2D    uWorldNormals   : register(t1);
            Texture2D    uWorldPositions : register(t2);
            Texture2D    uSceneDepth     : register(t3);
            SamplerState s0 : register(s0);
            SamplerState s1 : register(s1);
            SamplerState s2 : register(s2);
            SamplerState s3 : register(s3);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float EyeDepth(float d) {
                return abs(projDepth.y / (d + projDepth.x));
            }
            float4 PSMain(PSInput i) : SV_Target {
                float2 uv = i.fragCoord;
                float4 nrmC = uWorldNormals.SampleLevel(s1, uv, 0);
                if (dot(nrmC.xyz, nrmC.xyz) < 0.25)
                    return surface.SampleLevel(s0, uv, 0);
                float3 NC = normalize(nrmC.xyz);
                float3 PC = (distanceSource == 0) ? uWorldPositions.SampleLevel(s2, uv, 0).xyz : float3(0.0, 0.0, 0.0);
                float DC = (distanceSource == 0) ? 0.0 : EyeDepth(uSceneDepth.SampleLevel(s3, uv, 0).r);
                float2 dirStep = (direction < 0.5) ? float2(texelSize.x, 0.0) : float2(0.0, texelSize.y);
                float sigmaS = max(float(radius) * 0.5, 1.0);
                float4 sum = float4(0.0, 0.0, 0.0, 0.0);
                float sumW = 0.0;
                [loop] for (int k = -radius; k <= radius; ++k) {
                    float2 suv = uv + dirStep * float(k);
                    float4 nrmS = uWorldNormals.SampleLevel(s1, suv, 0);
                    if (dot(nrmS.xyz, nrmS.xyz) < 0.25)
                        continue;
                    float3 NS = normalize(nrmS.xyz);
                    float wS = exp(-float(k * k) / (2.0 * sigmaS * sigmaS));
                    float wN = pow(max(dot(NC, NS), 0.0), normalPower);
                    float dP;
                    if (distanceSource == 0) {
                        // Plane distance, NOT Euclidean: how far the neighbour lies off the centre
                        // pixel's tangent plane. A grazing same-surface (floor running to the horizon)
                        // stays ~0 and is kept; only real depth steps are rejected. The old
                        // length(PC-PS) blew up on grazing surfaces (screen-adjacent pixels sit far
                        // apart in world space) -> every tap rejected -> no blur -> raw noise survived.
                        float3 PS = uWorldPositions.SampleLevel(s2, suv, 0).xyz;
                        dP = abs(dot(PS - PC, NC));
                    }
                    else
                        dP = abs(EyeDepth(uSceneDepth.SampleLevel(s3, suv, 0).r) - DC);
                    float wP = exp(-(dP * dP) / (2.0 * posSigma * posSigma));
                    float w = wS * wN * wP;
                    sum += surface.SampleLevel(s0, suv, 0) * w;
                    sumW += w;
                }
                return (sumW > 0.0) ? sum / sumW : surface.SampleLevel(s0, uv, 0);
            }
        )"),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// =================================================================================================
