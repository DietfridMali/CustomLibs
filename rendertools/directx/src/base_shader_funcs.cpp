
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// HLSL helper function strings for the DirectX 12 backend.
//
// Convention:
//   Standard*VS()   - complete, self-contained HLSL vertex shader source
//   All others      - HLSL function bodies only (no cbuffer/texture declarations).
//                     The enclosing shader provides: b0 FrameConstants cbuffer,
//                     ShaderConstants cbuffer with the needed uniforms,
//                     Texture2D / SamplerState declarations, and static globals.
//
//   Static samplers (from root signature):
//     s0 = linear clamp   (screen-space / RenderTarget textures)
//     s1 = linear wrap    (world / material textures)
// =================================================================================================

// -------------------------------------------------------------------------------------------------
// Standard 2-D vertex shader (uses mViewport * mProjection * mModelView)
const String& Standard2DVS() {
    static const String source(R"(
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
        };
        PSInput VSMain(VSInput i) {
            PSInput o;
            float4 viewPos = mul(mModelView, float4(i.pos, 1.0));
            o.pos       = mul(mViewport, mul(mProjection, viewPos));
            o.fragCoord = i.tc;
            o.fragPos   = viewPos.xyz;
            return o;
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
// Standard 3-D vertex shader (uses mProjection * mModelView, no viewport)
const String& Standard3DVS() {
    static const String source(R"(
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
        };
        PSInput VSMain(VSInput i) {
            PSInput o;
            float4 viewPos = mul(mModelView, float4(i.pos, 1.0));
            o.pos       = mul(mProjection, viewPos);
            o.fragCoord = i.tc;
            o.fragPos   = viewPos.xyz;
            return o;
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
// 2-D vertex shader with scalar XY offset (mViewport * mProjection * mModelView + offset)
// Requires ShaderConstants cbuffer with: float offset;
const String& Offset2DVS() {
    static const String source(R"(
        cbuffer FrameConstants : register(b0) {
            column_major float4x4 mModelView;
            column_major float4x4 mProjection;
            column_major float4x4 mViewport;
            column_major float4x4 mLightTransform;
        };
        cbuffer ShaderConstants : register(b1) { float offset; };
        struct VSInput { float3 pos : POSITION; float2 tc : TEXCOORD; };
        struct PSInput {
            float4 pos       : SV_Position;
            float3 fragPos   : TEXCOORD0;
            float2 fragCoord : TEXCOORD1;
        };
        PSInput VSMain(VSInput i) {
            PSInput o;
            float4 viewPos  = mul(mModelView, float4(i.pos, 1.0));
            float4 shifted  = float4(viewPos.x + offset, viewPos.y + offset, viewPos.z, 1.0);
            o.pos       = mul(mViewport, mul(mProjection, shifted));
            o.fragCoord = i.tc;
            o.fragPos   = viewPos.xyz;
            return o;
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
// Gauss blur helpers (function bodies only).
// Enclosing PS must declare:
//   Texture2D        surface : register(t0);
//   SamplerState     s0;            // linear clamp
//   ShaderConstants: float2 texelSize; int blurStrength; float blurSpread;
const String& GaussBlurFuncs() {
    static const String source(R"(
        float4 GaussBlur7x7(float2 baseUV, float spread) {
            const int HALF = 3;
            static const int weight[7] = { 1, 6, 15, 20, 15, 6, 1 };
            float3 sumRGB = 0; float sumA = 0, wSum = 0;
            for (int j = -HALF; j <= HALF; ++j) {
                for (int i2 = -HALF; i2 <= HALF; ++i2) {
                    int w = weight[i2+HALF] * weight[j+HALF];
                    float4 c = surface.Sample(s0, baseUV + float2((float)i2,(float)j)*texelSize*spread);
                    sumRGB += c.rgb * c.a * (float)w;
                    sumA   += c.a   * (float)w;
                    wSum   += (float)w;
                }
            }
            float3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : (float3)0;
            return float4(rgb, sumA / wSum);
        }

        float4 GaussBlur5x5(float2 baseUV, float spread) {
            const int HALF = 2;
            static const int weight[5] = { 1, 4, 6, 4, 1 };
            float3 sumRGB = 0; float sumA = 0, wSum = 0;
            for (int j = -HALF; j <= HALF; ++j) {
                for (int i2 = -HALF; i2 <= HALF; ++i2) {
                    int w = weight[i2+HALF] * weight[j+HALF];
                    float4 c = surface.Sample(s0, baseUV + float2((float)i2,(float)j)*texelSize*spread);
                    sumRGB += c.rgb * c.a * (float)w;
                    sumA   += c.a   * (float)w;
                    wSum   += (float)w;
                }
            }
            float3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : (float3)0;
            return float4(rgb, sumA / wSum);
        }

        float4 GaussBlur3x3(float2 baseUV, float spread) {
            const int HALF = 1;
            static const int weight[3] = { 1, 2, 1 };
            float3 sumRGB = 0; float sumA = 0, wSum = 0;
            for (int j = -HALF; j <= HALF; ++j) {
                for (int i2 = -HALF; i2 <= HALF; ++i2) {
                    int w = weight[i2+HALF] * weight[j+HALF];
                    float4 c = surface.Sample(s0, baseUV + float2((float)i2,(float)j)*texelSize*spread);
                    sumRGB += c.rgb * c.a * (float)w;
                    sumA   += c.a   * (float)w;
                    wSum   += (float)w;
                }
            }
            float3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : (float3)0;
            return float4(rgb, sumA / wSum);
        }

        float4 GaussBlur(float2 baseUV, int strength, float spread) {
            int   s  = (strength < 0) ? blurStrength : strength;
            float sp = (spread   < 0) ? blurSpread   : spread;
            if (s == 3) 
                return GaussBlur7x7(baseUV, sp);
            if (s == 2) 
                return GaussBlur5x5(baseUV, sp);
            if (s == 1) 
                return GaussBlur3x3(baseUV, sp);
            return surface.Sample(s0, baseUV);
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
const String& BoostFuncs() {
    static const String source(R"(
        float Boost(float v, float strength) {
            return (v < 0.5) ? pow(v, 1.0/strength) : pow(v, strength);
        }

        float3 Boost(float3 v, float strength) {
            return float3(Boost(v.x,strength), Boost(v.y,strength), Boost(v.z,strength));
        }

        float SmoothBoost(float v, float strength) {
            float dark  = pow(v, strength);
            float light = pow(v, 1.0/strength);
            return lerp(dark, light, smoothstep(0.45, 0.55, v));
        }

        float3 SmoothBoost(float3 v, float strength) {
            return float3(SmoothBoost(v.r,strength), SmoothBoost(v.g,strength), SmoothBoost(v.b,strength));
        }

        float SinBoost(float v, float strength) {
            return sin(Boost(v, strength) * 0.5 * 3.14159265358979f);
        }

        float3 SinBoost(float3 v, float strength) {
            return float3(SinBoost(v.r,strength), SinBoost(v.g,strength), SinBoost(v.b,strength));
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
const String& SRGBFuncs() {
    static const String source(R"(
        float3 ToLinear(float3 c) { return pow(c, 2.2); }
        float3 ToSRGB(float3 c)   { return pow(max(c, 0.0), 1.0/2.2); }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
const String& TintFuncs() {
    static const String source(R"(
        float3 ApplyExponentialTint(float3 color, float3 tintScale, float e) {
            float3 s = pow(max(tintScale, 1e-6), max(e, 0.0));
            float3 denom = max(color * s, 1e-6);
            float  t = min(1.0, min(1.0/denom.r, min(1.0/denom.g, 1.0/denom.b)));
            return (color * t) * s;
        }
        float3 ApplyTint(float3 color, float3 tintScale) {
            return ApplyExponentialTint(color, tintScale, 1.0);
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
const String& NoiseFuncs() {
    static const String source(R"(
        float hash12(float2 p) {
            return frac(sin(dot(p, float2(127.1,311.7))) * 43758.5453);
        }
        float valueNoise2D(float2 x) {
            float2 i = floor(x), f = frac(x);
            f = f*f*(3.0 - 2.0*f);
            float a = hash12(i+float2(0,0)), b = hash12(i+float2(1,0));
            float c = hash12(i+float2(0,1)), d = hash12(i+float2(1,1));
            return lerp(lerp(a,b,f.x), lerp(c,d,f.x), f.y);
        }
        float2 noiseVec2(float2 x) {
            return 2.0*float2(valueNoise2D(x), valueNoise2D(x+13.37)) - 1.0;
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------

const String& RandFuncs() {
    static const String source(R"(
        static uint _rngState;
        void seedRand(float s) {
            uint u = (uint)(s * 4096.0);
            u ^= 0x9E3779B9u;
            _rngState = (u == 0u) ? 1u : u;
        }
        uint _lcg() { _rngState = _rngState * 1664525u + 1013904223u; return _rngState; }
        float rand() { return (float)_lcg() * (1.0/4294967296.0); }
        int   randn(int n) { return (int)(_lcg() % (uint)max(n,1)); }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
// EdgeFadeFunc: references uniform 'edgeFade' (must be in ShaderConstants).
const String& EdgeFadeFunc() {
    static const String source(R"(
        float2 EdgeFade(float2 baseUV, float2 dispUV) {
            float ef = clamp(edgeFade, 0.0, 0.5);
            if (ef > 1e-6) {
                float2 edgeXY  = min(baseUV, 1.0 - baseUV);
                float  edgeMin = min(edgeXY.x, edgeXY.y);
                float  w = smoothstep(0.0, ef, edgeMin);
                dispUV *= w;
                float2 limit = max(edgeXY - 1e-4, 0.0);
                dispUV = clamp(dispUV, -limit, limit);
            }
            return dispUV;
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
// ChromAbFuncs: references Texture2D surface (t0), SamplerState s0,
//   uniforms: float aberration, int offsetType, float2 viewportSize
const String& ChromAbFuncs() {
    static const String source(R"(
        float2 LinearOffset(float2 uv) {
            return uv * (0.6*aberration + 1e-4);
        }
        float2 RadialOffset(float2 uv) {
            float  aspect = viewportSize.x / max(viewportSize.y, 1.0);
            float2 d = uv - 0.5;
            float2 m = float2(d.x*aspect, d.y);
            float  rM = length(m);
            float2 dirM = (rM > 0.0) ? (m/rM) : (float2)0;
            float  L = aberration * rM;
            return float2((L*dirM.x)/max(aspect,1e-6), L*dirM.y);
        }
        float3 ChromaticAberration(float2 baseUV, float2 dispUV) {
            float2 off = (offsetType == 1) ? RadialOffset(baseUV) : LinearOffset(dispUV);
            return float3(surface.Sample(s0,baseUV+off).r,
                          surface.Sample(s0,baseUV    ).g,
                          surface.Sample(s0,baseUV-off).b);
        }
        float3 ChromaticAberration(float2 baseUV, float2 dispUV, float3 baseColor) {
            if (aberration < 1e-6) return baseColor;
            float2 off = (offsetType == 1) ? RadialOffset(baseUV) : LinearOffset(dispUV);
            float3 c0  = surface.Sample(s0, baseUV).rgb;
            return baseColor + float3(surface.Sample(s0,baseUV+off).r - c0.r,
                                      0.0,
                                      surface.Sample(s0,baseUV-off).b - c0.b);
        }
    )");
    return source;
}


// -------------------------------------------------------------------------------------------------
// VignetteFunc: uses static float2 fragCoord (set in PSMain),
//   uniform float vignetteRadius (in ShaderConstants).
const String& VignetteFunc() {
    static const String source(R"(
        float Vignette() {
            const float vignetteBlur = 0.25;
            float dist  = distance(fragCoord, float2(0.5,0.5)) / 0.7071;
            float edge0 = vignetteRadius;
            float edge1 = min(1.0, vignetteRadius + vignetteBlur);
            return smoothstep(edge1, edge0, dist);
        }
    )");
    return source;
}

// =================================================================================================
