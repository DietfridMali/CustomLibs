
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const String& Standard2DVS() {
    static const String source(
        R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 texCoord;
            uniform mat4 mModelView;
            uniform mat4 mProjection;
            uniform mat4 mViewport;
            out vec3 fragPos;
            out vec2 fragCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mViewport * mProjection * viewPos;
                fragCoord = texCoord;
                fragPos = viewPos.xyz;
                }
        )"
    );
    return source;
}

const String& Standard3DVS() {
    static const String source(
        R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 texCoord;
            uniform mat4 mModelView;
            uniform mat4 mProjection;
            out vec3 fragPos;
            out vec2 fragCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mProjection * viewPos;
                fragCoord = texCoord;
                fragPos = viewPos.xyz;
                }
        )"
    );
    return source;
}

const String& Offset2DVS() {
    static const String source(
        R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 texCoord;
            uniform mat4 mModelView;
            uniform mat4 mProjection;
            uniform mat4 mViewport;
            uniform float offset;
            out vec3 fragPos;
            out vec2 fragCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mViewport * mProjection * vec4(viewPos.x + offset, viewPos.y + offset, viewPos.z, 1.0);
                fragCoord = texCoord;
                fragPos = viewPos.xyz;
                }
        )"
    );
    return source;
}


const String& GaussBlurFuncs() {
    static const String source(
        R"(
        uniform vec2 texelSize;
        uniform int blurStrengh;
        uniform float blurSpread;

        vec4 GaussBlur7x7(vec2 baseUV, float spread) {
            const int HALF = 3;
            const int weight[7] = int[](1, 6, 15, 20, 15, 6, 1);

            vec3 sumRGB = vec3(0.0);
            float sumA = 0.0;
            float wSum = 0.0;

            for (int j = -HALF; j <= HALF; ++j) {
                int jy = j + HALF;
                for (int i = -HALF; i <= HALF; ++i) {
                    int ix = i + HALF;
                    int w = weight[ix] * weight[jy];
                    vec2 offset = vec2(float(i), float(j)) * texelSize * spread;
                    vec4 c = texture(source, baseUV + offset);
                    sumRGB += c.rgb * c.a * w; // premultiplied
                    sumA   += c.a * w;
                    wSum   += w;
                }
            }
            vec3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : vec3(0.0);
            float a  = sumA / wSum;
            return vec4(rgb, a);
        }


        vec4 GaussBlur5x5(vec2 baseUV, float spread) {
            const int HALF = 2;
            const int weight[5] = int[](1, 4, 6, 4, 1);

            vec3 sumRGB = vec3(0.0);
            float sumA = 0.0;
            float wSum = 0.0;

            for (int j = -HALF; j <= HALF; ++j) {
                int jy = j + HALF;
                for (int i = -HALF; i <= HALF; ++i) {
                    int ix = i + HALF;
                    int w = weight[ix] * weight[jy];
                    vec2 offset = vec2(float(i), float(j)) * texelSize * spread;
                    vec4 c = texture(source, baseUV + offset);
                    sumRGB += c.rgb * c.a * w; // premultiplied
                    sumA   += c.a * w;
                    wSum   += w;
                }
            }
            vec3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : vec3(0.0);
            float a  = sumA / wSum;
            return vec4(rgb, a);
        }   

        vec4 GaussBlur3x3(vec2 baseUV, float spread) {
            const int HALF = 1;
            const int weight[3] = int[](1, 2, 1);

            vec3 sumRGB = vec3(0.0);
            float sumA = 0.0;
            float wSum = 0.0;

            for (int j = -HALF; j <= HALF; ++j) {
                int jy = j + HALF;
                for (int i = -HALF; i <= HALF; ++i) {
                    int ix = i + HALF;
                    int w = weight[ix] * weight[jy];
                    vec2 offset = vec2(float(i), float(j)) * texelSize * spread;
                    vec4 c = texture(source, baseUV + offset);
                    sumRGB += c.rgb * c.a * w; // premultiplied
                    sumA   += c.a * w;
                    wSum   += w;
                }
            }
            vec3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : vec3(0.0);
            float a  = sumA / wSum;
            return vec4(rgb, a);
        }

        vec4 GaussBlur(vec2 baseUV, int strength, float spread) {
            switch((strength < 0) ? blurStrengh : strength) {
                case 3:
                    return GaussBlur7x7(baseUV, (spread < 0) ? blurSpread : spread);
                case 2:
                    return GaussBlur5x5(baseUV, (spread < 0) ? blurSpread : spread);
                case 1:
                    return GaussBlur3x3(baseUV, (spread < 0) ? blurSpread : spread);
                default:
                    return texture(source, baseUV);
            }
        }
      )"
    );
    return source;
};


const String& BoostFuncs() {
    static const String source(R"(
        float Boost(float v, float strength) { 
            return (v < 0.5) ? pow(v, 1.0 / strength) : pow(v, strength); 
        }
        
        vec3 Boost(vec3 v, float strength) { 
            return vec3(Boost(v.x, strength), Boost(v.y, strength), Boost(v.z, strength)); 
        }

        float SmoothBoost(float v, float strength) {
            float dark = pow(v, strength);                 // < 0.5 dunkler (strength>1)
            float light = pow(v, 1.0/strength);            // >= 0.5 heller
            float blend = smoothstep(0.45, 0.55, v);
            return mix(dark, light, blend);
        }

        vec3 SmoothBoost(vec3 v, float strength) { 
            return vec3(SmoothBoost(v.r,strength), SmoothBoost(v.g,strength), SmoothBoost(v.b,strength)); 
        }

        float SinBoost(float v, float strength) {
    	    const float PI = 3.14159265358979323846f;
            return sin(Boost(v, strength) * 0.5 * PI);
        }

        vec3 SinBoost(vec3 v, float strength) { 
            return vec3(SinBoost(v.r,strength), SinBoost(v.g,strength), SinBoost(v.b,strength)); 
        }
    )");
    return source;
}


const String& SRGBFuncs() {
    static const String source(R"(
        vec3 ToLinear(vec3 c) { return pow(c, vec3(2.2)); }

        vec3 ToSRGB(vec3 c) { return pow(max(c, 0.0), vec3(1.0 / 2.2)); }
    )");
    return source;
}


const String& TintFuncs() {
    static const String source(R"(
        // downscale color just so much inf need be that tint can be fully applied
        vec3 ApplyExponentialTint(vec3 color, vec3 tintScale, float e) {
            // exponentiell verstärkter Tint
            vec3 s = pow(max(tintScale, vec3(1e-6)), vec3(max(e, 0.0)));
            // determine downscale factor to avoid color overrun when applying tint
            vec3 denom = max(color * s, vec3(1e-6));
            vec3 inv   = 1.0 / denom;
            float t    = min(1.0, min(inv.r, min(inv.g, inv.b)));
            // scale color down, then apply tint
            return (color * t) * s;
        }

        vec3 ApplyTint(vec3 color, vec3 tintScale) {
            return ApplyExponentialTint(color, tintScale, 1.0);
        }
    )");
    return source;
}

const String& NoiseFuncs() {
    static const String source(R"(
        float hash12(vec2 p) {
            return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
        }
        float valueNoise2D(vec2 x) {
            vec2 i = floor(x);
            vec2 f = fract(x);
            f = f*f*(3.0 - 2.0*f);
            float a = hash12(i + vec2(0.0, 0.0));
            float b = hash12(i + vec2(1.0, 0.0));
            float c = hash12(i + vec2(0.0, 1.0));
            float d = hash12(i + vec2(1.0, 1.0));
            return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
        }
        vec2 noiseVec2(vec2 x) {
            float n1 = valueNoise2D(x);
            float n2 = valueNoise2D(x + 13.37);
            return 2.0 * vec2(n1, n2) - 1.0; // [-1..1]
        }
    )");
    return source;
}


const String& RandFuncs() {
    static const String source(R"(
        uint _rngState;

        void seedRand(float s) {
            // simple float->uint hash for seeding; scale s to avoid tiny deltas
            uint u = uint(s * 4096.0);
            u ^= 0x9E3779B9u;               // mix in a constant
            _rngState = (u == 0u) ? 1u : u; // avoid zero state
        }

        uint _lcg() {
            _rngState *= 1664525u + 1013904223u; // LCG
            return _rngState;
        }

        float rand() {
            return float(_lcg()) * (1.0 / 4294967296.0); // [0..1)
        }

        int randn(int n) {
            return int(_lcg() % uint(max(n, 1)));        // 0..n-1
        }
    )");
    return source;
}


const String& EdgeFadeFunc() {
    static const String source(R"(
        uniform float edgeFade;
        vec2 EdgeFade(vec2 baseUV, vec2 dispUV) {
            float ef = clamp(edgeFade, 0.0, 0.5);
            if (ef > 1e-6) {
                vec2 edgeXY = min(baseUV, 1.0 - baseUV);
                float edgeMin = min(edgeXY.x, edgeXY.y);
                float w = smoothstep(0.0, ef, edgeMin);
                dispUV *= w;
                // Bounds cap to keep finalUV safely inside [0,1]
                vec2 limit = max(edgeXY - vec2(1e-4), vec2(0.0));
                dispUV = clamp(dispUV, -limit, limit);
            }
            return dispUV;
        }
    )");
    return source;
}


const String& ChromAbFuncs() {
    static const String source(R"(
        // === Chromatic Aberration (UV-space) ===
        // Uses existing uniforms: sampler2D source, vec2 viewportSize
        uniform float aberration;
        uniform int   offsetType;       // 0 = linear, 1 = radial

        // Build linear CA offset in UV units
        vec2 LinearOffset(vec2 uv) {
            return uv * (0.6 * aberration + 1e-4);
        }

        // Build radial CA offset in UV units (aspect-correct, circular around center)
        vec2 RadialOffset(vec2 uv) {
            float aspect = viewportSize.x / max(viewportSize.y, 1.0);
            vec2 d = uv - 0.5;                // UV delta from center
            vec2 m = vec2(d.x * aspect, d.y); // metric space (x scaled by aspect)
            float rM = length(m);               // radial distance in metric space
            vec2 dirM = (rM > 0.0) ? (m / rM) : vec2(0.0); // direction (metric)
            float L = aberration * rM;        // fringe grows with radius
            // Map metric direction back to UV
            return vec2((L * dirM.x) / max(aspect, 1e-6), L * dirM.y);
        }

        // Pure CA: compose RGB from source only (useful in a dedicated CA pass)
        vec3 ChromaticAberration(vec2 baseUV, vec2 dispUV) {
            vec2 offset = (offsetType == 1) ? RadialOffset(baseUV) : LinearOffset(dispUV);
            float rC = texture(source, baseUV + offset).r;
            float gC = texture(source, baseUV).g;
            float bC = texture(source, baseUV - offset).b;
            return vec3(rC, gC, bC);
        }

        // Hybrid CA (Delta-Fringe): add only the RB fringe to the already-processed baseColor
        // Keeps your blur/tint/gray intact and costs only 2 extra fetches.
        vec3 ChromaticAberration(vec2 baseUV, vec2 dispUV, vec3 baseColor) {
            if (aberration < 1e-6)
                return baseColor;
            vec2 offset = (offsetType == 1) ? RadialOffset(baseUV) : LinearOffset(dispUV);
            vec3 c0 = texture(source, baseUV).rgb;
            float rS = texture(source, baseUV + offset).r;
            float bS = texture(source, baseUV - offset).b;
            vec3 fringe = vec3(rS - c0.r, 0.0, bS - c0.b);
            return baseColor + fringe;
        }
    )");
    return source;
}


const String& VignetteFunc() {
    static const String source(R"(
        uniform float vignetteRadius = 0.25f;
        const float vignetteBlur = 0.25; // konstant, kann auch Uniform werden

        float Vignette() {
            // Mittelpunkt in NDC
            vec2 uv = fragCoord;
            vec2 center = vec2(0.5, 0.5);
            float dist = distance(uv, center) / 0.7071; // max Abstand Ecke ~ sqrt(0.5) ~ 0.7071
            float edge0 = vignetteRadius;
            float edge1 = min(1.0, vignetteRadius + vignetteBlur);
            return smoothstep(edge1, edge0, dist);
        }
    )");
    return source;
}

// =================================================================================================
