#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// This shader lives in rendertools because the draw it belongs to does: Skybox::Render () (src/skybox.cpp)
// asks its shader handler for the id "skybox", and it did so from a source that only Paintjob Rampage
// registered - so anybody else using the class got a null shader and an empty sky. Anything an
// application wants to look different here is a uniform, not a second copy of the source.
//
// The cube is drawn at maximum depth (the xyww trick makes z == w, so the depth test sees 1.0) with
// LEQUAL and no depth write: it fills whatever the scene left at the far plane. mView must be a pure
// rotation about the camera - the caller nulls the translation column.
//
// lightDirection only turns the cube map about Y, so the sun in the texture ends up where the light
// comes from. brightness does two things: it picks the blend of the three cloud cover variants
// (1.0 = sky1, 0.85 = sky2, 0.7 = sky3) and scales the result, which is what makes an overcast sky a
// darker one as well.

static const ShaderDataAttributes VtxAttrs[] = {
    { "Vertex", 0, ShaderDataAttributes::Float3 },
};

const ShaderSource& SkyboxShader() {
    static const ShaderSource source(
        "skybox",
        R"(
            cbuffer FrameConstants : register(b0) {
                column_major float4x4 mModelView;
                column_major float4x4 mProjection;
            };
            cbuffer ShaderConstants : register(b1) {
                column_major float4x4 mView;
            };
            struct VSInput { [[vk::location(0)]] float3 pos : POSITION; };
            struct PSInput {
                float4 pos          : SV_Position;
                float3 viewDirection : TEXCOORD0;
            };
            PSInput VSMain(VSInput i) {
                PSInput o;
                float4 clipPos = mul(mProjection, mul(mView, float4(i.pos, 1.0)));
                o.pos  = float4(clipPos.xy, clipPos.w, clipPos.w);
                o.viewDirection = i.pos;
                return o;
            }
        )",
        R"(
            cbuffer ShaderConstants : register(b1) {
                column_major float4x4 mView;
                float3  lightDirection;
                float   brightness;
                float   alpha;
            };
            TextureCube  sky1 : register(t0);
            TextureCube  sky2 : register(t1);
            TextureCube  sky3 : register(t2);
            SamplerState s0   : register(s0);
            SamplerState s1   : register(s1);
            SamplerState s2   : register(s2);
            struct PSInput {
                float4 pos          : SV_Position;
                float3 viewDirection : TEXCOORD0;
            };
            static const float PI = 3.141592653589793;
            float3 YRotate(float3 v, float a) {
                float c = cos(a), s = sin(a);
                return float3(c*v.x + s*v.z, v.y, -s*v.x + c*v.z);
            }
            float4 PSMain(PSInput i) : SV_Target {
                float3 viewDir = normalize(i.viewDirection);
                float2 h = float2(lightDirection.x, lightDirection.z);
                float l = length(h);
                float yaw = (l > 1e-5) ? -atan2(h.x, h.y) : 0.0;
                float3 sampleDir = normalize(YRotate(viewDir, yaw));

                float coverage = 1.0 - clamp((brightness - 0.7) / 0.3, 0.0, 1.0);
                float3 c2 = sky2.Sample(s1, sampleDir).rgb;
                float3 color;
                if (coverage < 0.5) {
                    float3 c1 = sky1.Sample(s0, sampleDir).rgb;
                    float a = smoothstep(0.0, 0.5, coverage);
                    color = lerp(c1, c2, a);
                } else {
                    float3 c3 = sky3.Sample(s2, sampleDir).rgb;
                    float  a = smoothstep(0.5, 1.0, coverage);
                    color = lerp(c2, c3, a);
                }
                return float4(color * brightness, alpha);
            }
        )",
        ShaderDataLayout(VtxAttrs, 1)
    );
    return source;
}

// =================================================================================================
