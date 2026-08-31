#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// Tone mapping: an HDR render target onto the screen. See the OpenGL backend's copy for what the
// curve does and why a renderer that lights into a floating point target needs one.

static const ShaderDataAttributes VtxTcAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};

// The VS is Offset2DVS(), which declares b1 with 'float offset'. The PS b1 starts with the same
// 'float vsOffset' so the combined cbuffer layout stays consistent across VS and PS.
const ShaderSource& ToneMapShader() {
    static const ShaderSource toneMapShader(
        "tonemap",
        Offset2DVS(),
        String(R"(
            cbuffer ShaderConstants : register(b1) {
                float vsOffset;   // VS 'offset' lives at byte 0; PS ignores it
                float exposure;
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };

            static const float startCompression = 0.8 - 0.04;
            static const float desaturation = 0.15;

            float3 ToneMap(float3 c) {
                c *= exposure;
                float peak = max(c.r, max(c.g, c.b));
                if (peak < startCompression)
                    return c;
                float d = 1.0 - startCompression;
                float newPeak = 1.0 - d * d / (peak + d - startCompression);
                c *= newPeak / peak;
                float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
                return lerp(c, float3(newPeak, newPeak, newPeak), g);
            }

            float4 PSMain(PSInput i) : SV_Target {
                float4 sceneColor = surface.Sample(s0, i.fragCoord);
                return float4(ToneMap(sceneColor.rgb), sceneColor.a);
            }
        )"),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return toneMapShader;
}

// =================================================================================================
