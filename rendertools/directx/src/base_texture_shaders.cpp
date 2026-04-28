
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// HLSL shader strings for the DirectX 12 backend — base texture / utility shaders.
//
// Convention (same as base_shader_funcs.cpp):
//   Standard2DVS() is used as the VS for 2-D full-screen / UI passes.
//   All PS strings are self-contained (declare own cbuffer, textures, PSInput struct).
//   Static samplers from root signature: s0 = linear clamp, s1 = linear wrap.
// =================================================================================================

static const ShaderDataAttributes VtxAttrs[] = {
    { "Vertex", 0, ShaderDataAttributes::Float3 },
};

static const ShaderDataAttributes VtxTcAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};

// -------------------------------------------------------------------------------------------------
// Hardcoded-triangle test: no vertex buffer needed, just SV_VertexID.
const ShaderSource& TestShader() {
    static const ShaderSource source(
        "testShader",
        R"(
            struct PSInput { float4 pos : SV_Position; };
            PSInput VSMain(uint vertexID : SV_VertexID) {
                float2 positions[3];
                positions[0] = float2(-0.5, -0.5);
                positions[1] = float2( 0.5, -0.5);
                positions[2] = float2( 0.0,  0.5);
                PSInput o;
                o.pos = float4(positions[vertexID], 0.0, 1.0);
                return o;
            }
        )",
        R"(
            struct PSInput { float4 pos : SV_Position; };
            float4 PSMain(PSInput i) : SV_Target {
                return float4(1.0, 0.0, 1.0, 1.0);
            }
        )"
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Stencil pass: transforms position through viewport*projection, PS discards everything
// (depth/stencil write only).
const ShaderSource& StencilShader() {
    static const ShaderSource source(
        "stencilShader",
        R"(
            cbuffer FrameConstants : register(b0) {
                column_major float4x4 mModelView;
                column_major float4x4 mProjection;
                column_major float4x4 mViewport;
            };
            struct VSInput { float3 pos : POSITION; };
            struct PSInput { float4 pos : SV_Position; };
            PSInput VSMain(VSInput i) {
                PSInput o;
                o.pos = mul(mViewport, mul(mProjection, float4(i.pos, 1.0)));
                return o;
            }
        )",
        R"(
            struct PSInput { float4 pos : SV_Position; };
            float4 PSMain(PSInput i) : SV_Target {
                discard;
                return (float4)0;
            }
        )",
        ShaderDataLayout(VtxAttrs, 1)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Shadow/depth pass: projects through light transform, PS does alpha-cutout test.
const ShaderSource& DepthShader() {
    static const ShaderSource source(
        "depthShader",
        R"(
            cbuffer FrameConstants : register(b0) {
                column_major float4x4 mModelView;
                column_major float4x4 mProjection;
                column_major float4x4 mViewport;
                column_major float4x4 mLightTransform;
            };
            struct VSInput { float3 pos : POSITION; float2 tc : TEXCOORD; };
            struct PSInput {
                float4 pos       : SV_Position;
                float2 fragCoord : TEXCOORD0;
            };
            PSInput VSMain(VSInput i) {
                PSInput o;
                o.pos       = mul(mLightTransform, mul(mModelView, float4(i.pos, 1.0)));
                o.pos.z     = o.pos.z * 0.5 + 0.5 * o.pos.w;
                o.fragCoord = i.tc;
                return o;
            }
        )",
        R"(
            cbuffer ShaderConstants : register(b1) { float4 surfaceColor; };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float2 fragCoord : TEXCOORD0;
            };
            float4 PSMain(PSInput i) : SV_Target {
                if (surface.Sample(s0, i.fragCoord).a * surfaceColor.a < 0.9)
                    discard;
                return (float4)0;
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2, 0)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Sphere shadow pass: projects sphere vertex through light transform (no face culling active).
const ShaderSource& SphereDepthShader() {
    static const ShaderSource source(
        "sphereDepthShader",
        R"(
            cbuffer FrameConstants : register(b0) {
                column_major float4x4 mModelView;
                column_major float4x4 mProjection;
                column_major float4x4 mViewport;
                column_major float4x4 mLightTransform;
            };
            struct VSInput { float3 pos : POSITION; };
            struct PSInput { float4 pos : SV_Position; };
            PSInput VSMain(VSInput i) {
                PSInput o;
                o.pos   = mul(mLightTransform, mul(mModelView, float4(i.pos, 1.0)));
                o.pos.z = o.pos.z * 0.5 + 0.5 * o.pos.w;
                return o;
            }
        )",
        R"(
            struct PSInput { float4 pos : SV_Position; };
            float4 PSMain(PSInput i) : SV_Target {
                return (float4)0;
            }
        )",
        ShaderDataLayout(VtxAttrs, 1, 0)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Debug visualiser: renders a depth texture as a linearised greyscale image.
const ShaderSource& DepthRenderer() {
    static const ShaderSource source(
        "depthRenderer",
        Standard2DVS(),
        R"(
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float d = surface.Sample(s0, float2(i.fragCoord.x, 1.0 - i.fragCoord.y)).r;
                d = pow(d, 0.5);
                return float4(d, d, d, 1.0);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Solid colour fill (no texture).
static const ShaderDataAttributes VtxColorAttrs[] = {
    { "Vertex", 0, ShaderDataAttributes::Float3 },
    { "Color",  0, ShaderDataAttributes::Float4 },
};

const ShaderSource& ColorMeshShader() {
    static const ShaderSource source(
        "colorMesh",
        R"(
            cbuffer FrameConstants : register(b0) {
                column_major float4x4 mModelView;
                column_major float4x4 mProjection;
                column_major float4x4 mViewport;
            };
            struct VSInput { float3 pos : POSITION; float4 color : COLOR; };
            struct PSInput {
                float4 pos          : SV_Position;
                float4 surfaceColor : COLOR;
            };
            PSInput VSMain(VSInput i) {
                PSInput o;
                float4 viewPos = mul(mModelView, float4(i.pos, 1.0));
                o.pos          = mul(mViewport, mul(mProjection, viewPos));
                o.surfaceColor = i.color;
                return o;
            }
        )",
        R"(
            cbuffer ShaderConstants : register(b1) { int premultiply; };
            struct PSInput {
                float4 pos          : SV_Position;
                float4 surfaceColor : COLOR;
            };
            float4 PSMain(PSInput i) : SV_Target {
                return i.surfaceColor;
            }
        )",
        ShaderDataLayout(VtxColorAttrs, 2)
    );
    return source;
}


const ShaderSource& PlainColorShader() {
    static const ShaderSource source(
        "plainColor",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) { float4 surfaceColor; };
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                return surfaceColor;
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Greyscale conversion with brightness and optional invert.
const ShaderSource& GrayScaleShader() {
    static const ShaderSource source(
        "grayScale",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float2 tcOffset;
                float2 tcScale;
                float  brightness;
                int    invert;
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float4 texColor = surface.Sample(s0, tcOffset + i.fragCoord * tcScale);
                // Rec.601 luma weights in gamma space
                float gray = dot(texColor.rgb, float3(0.299, 0.587, 0.114));
                gray *= brightness;
                float3 rgb = (invert != 0) ? float3(1.0 - gray, 1.0 - gray, 1.0 - gray)
                                           : float3(gray, gray, gray);
                return float4(rgb, texColor.a);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// -------------------------------------------------------------------------------------------------
// Textured quad with per-fragment tint colour; alpha zero → discard.
const ShaderSource& PlainTextureShader() {
    static const ShaderSource source(
        "plainTexture",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float4 surfaceColor;
                float2 tcOffset;
                float2 tcScale;
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float4 texColor = surface.Sample(s0, tcOffset + tcScale * i.fragCoord);
                float a = texColor.a * surfaceColor.a;
                if (a == 0) discard;
                return float4(texColor.rgb * surfaceColor.rgb, a);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

const ShaderSource& GlyphShader() {
    static const ShaderSource source(
        "glyph",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float4 surfaceColor;
                float2 tcOffset;
                float2 tcScale;
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float4 texColor = surface.Sample(s0, tcOffset + tcScale * i.fragCoord);
                float a = texColor.a * surfaceColor.a;
                return float4(1.0, 0.5, 0.0, 1.0);
                return float4(a, a, a, 1);
                if (a == 0) //discard;
                  return float4(0.0, 0.5, 1.0, 1.0);
                float3 rgb = texColor.rgb * surfaceColor.rgb;
                if (all(texColor.rgb == float3(0.0, 0.0, 0.0)))
                  return float4(1.0, 0.5, 0.0, 1.0);
                return float4(texColor.rgb * surfaceColor.rgb, a);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// -------------------------------------------------------------------------------------------------
// Scrolling / animated texture with SmoothBoost contrast enhancement.
// ShaderConstants: surfaceColor, direction (scroll dir), speed, time.
const ShaderSource& MovingTextureShader() {
    static const ShaderSource source(
        "movingTexture",
        Standard2DVS(),
        String(R"(
            cbuffer ShaderConstants : register(b1) {
                float4 surfaceColor;
                float2 direction;
                float  speed;
                float  time;
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
        )") +
        BoostFuncs() +
        String(R"(
            float4 PSMain(PSInput i) : SV_Target {
                float4 texColor = surface.Sample(s0, i.fragCoord + direction * (time * speed));
                float3 rgbColor = texColor.rgb * surfaceColor.rgb;
                return float4(SmoothBoost(rgbColor, 2.0), 1.0);
            }
        )"),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Gaussian-blurred texture with tint colour; alpha zero → discard.
// ShaderConstants: surfaceColor, texelSize, blurStrength, blurSpread.
const ShaderSource& BlurTextureShader() {
    static const ShaderSource source(
        "blurTexture",
        Standard2DVS(),
        String(R"(
            cbuffer ShaderConstants : register(b1) {
                float4 surfaceColor;
                float2 texelSize;
                int    blurStrength;
                float  blurSpread;
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
                float4 texColor = GaussBlur(i.fragCoord, -1, -1);
                float a = texColor.a * surfaceColor.a;
                if (a == 0) discard;
                return float4(texColor.rgb * surfaceColor.rgb, a);
            }
        )"),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Gaussian blur + greyscale conversion + contrast/gamma/brightness + optional tint/invert.
// ShaderConstants: texelSize, blurStrength, blurSpread, brightness, contrast, gamma, invert, tint.
const ShaderSource& TintAndBlurShader() {
    static const ShaderSource source(
        "tintAndBlur",
        Standard2DVS(),
        String(R"(
            cbuffer ShaderConstants : register(b1) {
                float2 texelSize;
                int    blurStrength;
                float  blurSpread;
                float  brightness;
                float  contrast;
                float  gamma;
                int    invert;
                float4 tint;
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
        TintFuncs() +
        String(R"(
            float4 PSMain(PSInput i) : SV_Target {
                float4 texColor = GaussBlur(i.fragCoord, -1, -1);
                // Rec.601 luma in gamma space
                float gray = dot(texColor.rgb, float3(0.299, 0.587, 0.114));
                gray = (gray - 0.5) * contrast + 0.5;
                gray = clamp(gray, 0.0, 1.0);
                gray = pow(gray, 1.0 / gamma);
                gray *= brightness;
                float3 finalRGB = lerp(float3(gray, gray, gray), tint.rgb * gray, tint.a);
                float3 rgb = (invert != 0) ? float3(1.0, 1.0, 1.0) - finalRGB : finalRGB;
                return float4(rgb, texColor.a);
            }
        )"),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// =================================================================================================
