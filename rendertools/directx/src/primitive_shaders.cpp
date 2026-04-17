
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// HLSL shader strings for the DirectX 12 backend — 2-D primitive shaders.
//
// All shaders use Standard2DVS() except CircleShader (needs vertexY pass-through).
// PS strings are self-contained.  fwidth() is available natively in HLSL PS.
// bool uniforms are represented as int (0 = false, non-zero = true) in cbuffers.
// =================================================================================================

static const ShaderDataAttributes VtxTcAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};


// -------------------------------------------------------------------------------------------------
// SDF line segment with optional AA, round caps.
// ShaderConstants: viewportSize, start (UV), end (UV), surfaceColor, strength, antialias.
const ShaderSource& LineShader() {
    static const ShaderSource source(
        "lineShader",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float2 viewportSize;
                float2 start;
                float2 end;
                float4 surfaceColor;
                float  strength;
                int    antialias;
            };
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float2 pxFragCoord = i.fragCoord * viewportSize;
                float2 pxStart     = start * viewportSize;
                float2 pxEnd       = end   * viewportSize;
                float2 v           = pxEnd - pxStart;
                float  len2        = dot(v, v);
                float  pxStrength  = strength * min(viewportSize.x, viewportSize.y);
                float  r           = 0.5 * max(pxStrength, 0.0);
                float  d;
                if (len2 < 1e-6) {
                    d = length(pxFragCoord - pxStart) - r;
                } else {
                    float2 w    = pxFragCoord - pxStart;
                    float  t    = clamp(dot(w, v) / len2, 0.0, 1.0);
                    float2 proj = pxStart + t * v;
                    d = length(pxFragCoord - proj) - r;
                }
                float alpha;
                if (antialias != 0) {
                    float pxWidth = 0.5 * fwidth(d);
                    alpha = 1.0 - smoothstep(0.0, pxWidth, d);
                } else {
                    alpha = step(d, 0.0);
                }
                return float4(surfaceColor.rgb, surfaceColor.a * alpha);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// SDF ring / arc with optional AA and angular segment clipping.
// ShaderConstants: center (UV), radius, strength, surfaceColor, antialias, viewportSize,
//                  startAngle, endAngle (degrees, 0 = 12 o'clock, CW increasing).
const ShaderSource& RingShader() {
    static const ShaderSource source(
        "ringShader",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float2 center;
                float2 viewportSize;
                float4 surfaceColor;
                float  radius;
                float  strength;
                float  startAngle;
                float  endAngle;
                int    antialias;
            };
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            static const float PI = 3.14159265358979323846;
            float Rad2Deg(float r) { return r * (180.0 / PI); }
            // Angle with 0=12 o'clock, increasing clockwise.
            float Degrees(float2 d) {
                float r = 0.5 * PI - atan2(-d.y, d.x);
                r -= floor(r / (2.0 * PI)) * (2.0 * PI);
                return Rad2Deg(r);
            }
            float4 PSMain(PSInput i) : SV_Target {
                float  pxScale    = min(viewportSize.x, viewportSize.y);
                float2 pxDelta    = (i.fragCoord - center) * viewportSize;
                float  pxDist     = length(pxDelta);
                float  pxRadius   = radius   * pxScale;
                float  pxStrength = strength * pxScale;
                float  a          = Degrees(pxDelta);
                if (a < 0.0) a += 360.0;
                bool renderSegment = (startAngle != endAngle);
                if (renderSegment && (a < startAngle || a > endAngle))
                    discard;
                float outerR = pxRadius;
                float innerR = max(pxRadius - pxStrength, 0.0);
                float dOuter = pxDist - outerR;
                float dInner = innerR - pxDist;
                float alpha;
                if (antialias != 0) {
                    float pxWidth = 0.5 * fwidth(pxDist);
                    if (dOuter > pxWidth || dInner > pxWidth) discard;
                    float aOuter = 1.0 - smoothstep(0.0, pxWidth, dOuter);
                    float aInner = 1.0 - smoothstep(0.0, pxWidth, dInner);
                    alpha = aOuter * aInner;
                } else {
                    if (dOuter > 0.0 || dInner > 0.0) discard;
                    alpha = 1.0;
                }
                return float4(surfaceColor.rgb, surfaceColor.a * alpha);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// SDF filled circle with fill-level (lower half in greyscale when unfilled).
// Uses its own VS to pass vertexY.
// ShaderConstants: viewportSize, surfaceColor, center (UV), radius (UV), fillLevel, brightness, antialias.
const ShaderSource& CircleShader() {
    static const ShaderSource source(
        "circleShader",
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
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
                float  vertexY   : TEXCOORD2;
            };
            PSInput VSMain(VSInput i) {
                PSInput o;
                float4 viewPos = mul(mModelView, float4(i.pos, 1.0));
                o.pos      = mul(mViewport, mul(mProjection, viewPos));
                o.fragCoord = i.tc;
                o.fragPos   = viewPos.xyz;
                o.vertexY   = i.pos.y;
                return o;
            }
        )",
        R"(
            cbuffer ShaderConstants : register(b1) {
                float2 viewportSize;
                float2 center;
                float4 surfaceColor;
                float  radius;
                float  fillLevel;
                float  brightness;
                int    antialias;
            };
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
                float  vertexY   : TEXCOORD2;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float2 pxDelta  = (i.fragCoord - center) * viewportSize;
                float  pxDist   = length(pxDelta);
                float  pxRadius = radius * min(viewportSize.x, viewportSize.y);
                float  d        = pxDist - pxRadius;
                float  alpha;
                if (antialias != 0) {
                    float pxWidth = 0.5 * fwidth(pxDist);
                    if (d > pxWidth) discard;
                    alpha = 1.0 - smoothstep(0.0, pxWidth, d);
                } else {
                    if (d > 0.0) discard;
                    alpha = 1.0;
                }
                float  gray  = dot(surfaceColor.rgb, float3(0.299, 0.587, 0.114)) * brightness;
                float3 color = (i.vertexY <= fillLevel) ? surfaceColor.rgb
                                                        : float3(gray, gray, gray);
                return float4(color, surfaceColor.a * alpha);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// Circle mask: blends a texture inside a circular region.
// ShaderConstants: viewportSize, surfaceColor, maskColor, center, radius (UV),
//                  maskScale, antialias.
const ShaderSource& CircleMaskShader() {
    static const ShaderSource source(
        "circleMaskShader",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float2 viewportSize;
                float2 center;
                float4 surfaceColor;
                float4 maskColor;
                float  radius;
                float  maskScale;
                int    antialias;
            };
            Texture2D    surface : register(t0);
            SamplerState s0      : register(s0);
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float4 PSMain(PSInput i) : SV_Target {
                float2 fcDelta = i.fragCoord - center;
                float4 mask    = surface.Sample(s0, clamp(center + fcDelta * maskScale, 0.0, 1.0));
                if (mask.a > 0)
                    return mask;
                float2 pxDelta  = (i.fragCoord - center) * viewportSize;
                float  pxDist   = length(pxDelta);
                float  pxRadius = radius * min(viewportSize.x, viewportSize.y);
                float  d        = pxDist - pxRadius;
                float  alpha;
                if (antialias != 0) {
                    float pxWidth = 0.5 * fwidth(pxDist);
                    if (d > pxWidth) discard;
                    alpha = 1.0 - smoothstep(0.0, pxWidth, d);
                } else {
                    if (d > 0.0) discard;
                    alpha = 1.0;
                }
                return float4(surfaceColor.rgb, surfaceColor.a * alpha);
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}


// -------------------------------------------------------------------------------------------------
// SDF rounded rectangle, solid fill or stroke, optional AA.
// ShaderConstants: viewportSize, surfaceColor, center (UV), size (half-size UV),
//                  strength (stroke width, 0 = filled), radius, antialias.
const ShaderSource& RectangleShader() {
    static const ShaderSource source(
        "rectangleShader",
        Standard2DVS(),
        R"(
            cbuffer ShaderConstants : register(b1) {
                float2 viewportSize;
                float2 center;
                float2 size;
                float4 surfaceColor;
                float  strength;
                float  radius;
                int    antialias;
            };
            struct PSInput {
                float4 pos       : SV_Position;
                float3 fragPos   : TEXCOORD0;
                float2 fragCoord : TEXCOORD1;
            };
            float sdRoundRect(float2 pxFrag, float2 pxCenter, float2 pxHalf, float pxR) {
                float2 q = abs(pxFrag - pxCenter) - (pxHalf - float2(pxR, pxR));
                return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - pxR;
            }
            float4 PSMain(PSInput i) : SV_Target {
                float2 pxFragCoord = i.fragCoord * viewportSize;
                float2 pxCenter    = center * viewportSize;
                float2 pxSize      = size   * viewportSize;
                float  pxRadius    = clamp(radius * min(viewportSize.x, viewportSize.y),
                                           0.0, min(pxSize.x, pxSize.y));
                float sd  = sdRoundRect(pxFragCoord, pxCenter, pxSize, pxRadius);
                float aaw = (antialias != 0) ? fwidth(sd) : 0.0;
                if (strength <= 0.0) {
                    if (sd > aaw) discard;
                    float alpha = 1.0 - smoothstep(0.0, aaw, sd);
                    return float4(surfaceColor.rgb, surfaceColor.a * alpha);
                } else {
                    float pxStrength = strength * min(viewportSize.x, viewportSize.y);
                    float sdi = sd + pxStrength;
                    if (sd > aaw || sdi < -aaw) discard;
                    float covOuter = 1.0 - smoothstep(0.0, aaw, sd);
                    float covInner =       smoothstep(0.0, aaw, sdi);
                    float alpha    = covOuter * covInner;
                    if (alpha <= 0.0) discard;
                    return float4(surfaceColor.rgb, surfaceColor.a * alpha);
                }
            }
        )",
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// =================================================================================================
