#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

static const ShaderDataAttributes VtxTcAttrs[] = {
	{ "Vertex", 0, ShaderDataAttributes::Float3 },
	{ "TexCoord", 0, ShaderDataAttributes::Float2 },
};


const ShaderSource& XRLayerCopyShader()
{
	static const ShaderSource xrLayerCopyShader(
		"xrLayerCopy",
		Offset2DVS(),
		String(R"(
            cbuffer ShaderConstants : register(b1) {
                float vsOffset;
            };
            Texture2D surface : register(t0);
            SamplerState s0  : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };

            float3 SRGBToLinear(float3 c) {
                c = saturate(c);
                return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
            }

            float4 PSMain(PSInput i) : SV_Target {
                float4 c = surface.Sample(s0, i.fragCoord);
                if (c.a <= 0.0)
                    return float4(0.0, 0.0, 0.0, 0.0);
                return float4(SRGBToLinear(c.rgb / c.a) * c.a, c.a);
            }
        )"),
		ShaderDataLayout(VtxTcAttrs, 2));
	return xrLayerCopyShader;
}


const ShaderSource& XRLayerMirrorShader()
{
	static const ShaderSource xrLayerMirrorShader(
		"xrLayerMirror",
		String(R"(
            cbuffer FrameConstants : register(b0) {
                column_major float4x4 mModelView;
                column_major float4x4 mProjection;
                column_major float4x4 mViewport;
                FRAME_EYE_MATRICES
            };
            cbuffer ShaderConstants : register(b1) {
                column_major float4x4 mLayerToClip;
                int bDecodeSRGB;
                float fCentralAngle;
                float fRadius;
                float fCenterDistance;
                float fLayerHeight;
                float fStripStart;
                float fStripWidth;
            };
            struct VSInput { [[vk::location(0)]] float3 pos : POSITION; [[vk::location(1)]] float2 tc : TEXCOORD; };
            struct PSInput {
                float4 pos       : SV_Position;
                float2 fragCoord : TEXCOORD0;
            };
            PSInput VSMain(VSInput i) {
                PSInput o;
                if (fCentralAngle > 0.0) {
                    float u = fStripStart + (i.pos.x + 0.5) * fStripWidth;
                    float a = (u - 0.5) * fCentralAngle;
                    o.pos = mul(mViewport, mul(mLayerToClip, float4(fRadius * sin(a), i.pos.y * fLayerHeight, -fCenterDistance - fRadius * cos(a), 1.0)));
                    o.fragCoord = float2(u, i.tc.y);
                }
                else {
                    o.pos = mul(mViewport, mul(mLayerToClip, float4(i.pos.xy, 0.0, 1.0)));
                    o.fragCoord = i.tc;
                }
                return o;
            }
        )"),
		String(R"(
            cbuffer ShaderConstants : register(b1) {
                column_major float4x4 mLayerToClip;
                int bDecodeSRGB;
            };
            Texture2D surface : register(t0);
            SamplerState s0  : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float2 fragCoord : TEXCOORD0;
            };

            float3 SRGBToLinear(float3 c) {
                c = saturate(c);
                return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
            }

            float4 PSMain(PSInput i) : SV_Target {
                float4 c = surface.Sample(s0, i.fragCoord);
                if (bDecodeSRGB == 0)
                    return c;
                if (c.a <= 0.0)
                    return float4(0.0, 0.0, 0.0, 0.0);
                return float4(SRGBToLinear(c.rgb / c.a) * c.a, c.a);
            }
        )"),
		ShaderDataLayout(VtxTcAttrs, 2));
	return xrLayerMirrorShader;
}


const ShaderSource& XRMirrorShader()
{
	static const ShaderSource xrMirrorShader(
		"xrMirror",
		Offset2DVS(),
		String(R"(
            cbuffer ShaderConstants : register(b1) {
                float vsOffset;
                int   bEncodeSRGB;
                float2 tcOffset;
                float2 tcScale;
                float eyeLayer;
            };
            Texture2DArray eyeTex : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };

            float3 LinearToSRGB(float3 c) {
                c = saturate(c);
                return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
            }

            float4 PSMain(PSInput i) : SV_Target {
                float3 color = eyeTex.Sample(s0, float3(tcOffset + tcScale * i.fragCoord, eyeLayer)).rgb;
                if (bEncodeSRGB != 0)
                    color = LinearToSRGB(color);
                return float4(color, 1.0);
            }
        )"),
		ShaderDataLayout(VtxTcAttrs, 2));
	return xrMirrorShader;
}


const ShaderSource& XRDepthCopyShader()
{
	static const ShaderSource xrDepthCopyShader(
		"xrDepthCopy",
		Offset2DVS(),
		String(R"(
            cbuffer ShaderConstants : register(b1) {
                float vsOffset;
            };
            ScreenTexture sceneDepth : register(t0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };

            float PSMain(PSInput i VIEW_ID_PARAM) : SV_Depth {
                SET_VIEW_ID
                uint w;
                uint h;
                SCREEN_DIMENSIONS(sceneDepth, w, h)
                int2 xy = min(int2(i.fragCoord * float2(w, h)), int2(w, h) - 1);
                return sceneDepth.Load(SCREEN_TEXEL(xy)).r;
            }
        )"),
		ShaderDataLayout(VtxTcAttrs, 2, 0));
	return xrDepthCopyShader;
}

// =================================================================================================
