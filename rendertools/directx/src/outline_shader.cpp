
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// HLSL shader strings for the DirectX 12 backend — outline post-process shader.
// =================================================================================================

static const ShaderDataAttributes kVtxTcAttrs[] = {
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
        ShaderDataLayout(kVtxTcAttrs, 2)
    );
    return outlineShader;
}

// =================================================================================================
