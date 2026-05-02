
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
// 'coeffs' packs 4 coefficients per float4; C++ must upload as 9 × float4.
// ShaderConstants: vsOffset, direction (0=horiz,1=vert), radius (half-kernel size),
//                  texelSize, coeffs[9].
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
                float4 coeffs[9];   // 36 coefficients; coeffs[i] = coeffs[i/4][i%4]
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float GetCoeff(int i) {
                float4 v = coeffs[i >> 2];
                int r = i & 3;
                if (r == 0) 
                    return v.x;
                if (r == 1) 
                    return v.y;
                if (r == 2) 
                    return v.z;
                return v.w;
            }
            float4 PSMain(PSInput i) : SV_Target {
                float2 scrollDir = float2(1.0 - direction, direction);
                float4 sum = (float4)0;
                int n = 2 * radius + 1;
                for (int k = 0; k < n; ++k) {
                    float2 coord = i.fragCoord + scrollDir * float(k - radius) * texelSize;
                    sum += surface.Sample(s0, coord) * GetCoeff(k);
                }
                return float4(sum.rgb, sum.a);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return gaussBlurShader;
}

// =================================================================================================
