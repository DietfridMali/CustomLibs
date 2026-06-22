
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// HLSL shader strings for the DirectX 12 backend — outline post-process shader.
// =================================================================================================

static const ShaderDataAttributes VtxTcAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};


// -------------------------------------------------------------------------------------------------
// Outline: blends an outline colour around non-transparent pixels.
// Uses Offset2DVS().  textureSize() replaced by texelSize from cbuffer.
// ShaderConstants: vsOffset, texelSize, outlineColor, outlineWidth.
const ShaderSource& OutlineShader() {
    static const ShaderSource outlineShader(
        "outline",
        Offset2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float  vsOffset;      // VS 'offset'; PS ignores
                float  outlineWidth;  // in texels
                float2 texelSize;
                float4 outlineColor;
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float4 color = surface.Sample(s0, i.fragCoord);
                if (color.a > 0.0) {
                    return float4(lerp(outlineColor.rgb, color.rgb, color.a), 1.0);
                }
                float alpha = 0.0;
                float dx = outlineWidth * texelSize.x;
                int   r  = int(outlineWidth);
                for (int x = r; x >= 0; x--, dx -= texelSize.x) {
                    float dy = outlineWidth * texelSize.y;
                    for (int y = r; y >= 0; y--, dy -= texelSize.y) {
                        alpha = max(alpha, surface.SampleLevel(s0, i.fragCoord + float2(-dx, -dy), 0).a);
                        alpha = max(alpha, surface.SampleLevel(s0, i.fragCoord + float2(-dx,  dy), 0).a);
                        alpha = max(alpha, surface.SampleLevel(s0, i.fragCoord + float2( dx,  dy), 0).a);
                        alpha = max(alpha, surface.SampleLevel(s0, i.fragCoord + float2( dx, -dy), 0).a);
                    }
                }
                return (alpha > 0.0) ? float4(outlineColor.rgb, alpha) : (float4)0;
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return outlineShader;
}


// -------------------------------------------------------------------------------------------------
// Bevel: fake raised / "reverse emboss" look. Treats the source alpha as a height field, takes its
// screen-space gradient at the edge and lights it from lightDir — highlight on the side facing the light,
// shadow on the opposite side, flat interior (zero gradient) left unchanged. bevelWidth = edge sampling
// distance in texels (= width of the lit band ~ perceived thickness). ShaderConstants: texelSize, lightDir,
// bevelWidth, strength.
const ShaderSource& BevelShader() {
    static const ShaderSource bevelShader(
        "bevel",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float2 texelSize;    // 1 / buffer size
                float2 lightDir;     // screen-space direction to the light (y down); normalized in-shader
                float  bevelWidth;   // edge sampling distance in texels
                float  strength;     // emboss intensity
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float4 color = surface.Sample(s0, i.fragCoord);
                float2 o  = bevelWidth * texelSize;
                float  aL = surface.SampleLevel(s0, i.fragCoord - float2(o.x, 0), 0).a;
                float  aR = surface.SampleLevel(s0, i.fragCoord + float2(o.x, 0), 0).a;
                float  aU = surface.SampleLevel(s0, i.fragCoord - float2(0, o.y), 0).a;
                float  aD = surface.SampleLevel(s0, i.fragCoord + float2(0, o.y), 0).a;
                float2 grad = float2(aR - aL, aD - aU);                      // uphill of the alpha height field
                float  lit  = dot(-grad, normalize(lightDir)) * strength;    // + toward light, - away from it
                float3 rgb  = saturate(color.rgb * (1.0 + lit));
                return float4(rgb, color.a);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return bevelShader;
}

// =================================================================================================
