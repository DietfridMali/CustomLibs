
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
        uniform int blurStrength;
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
                    vec4 c = texture(surface, baseUV + offset);
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
                    vec4 c = texture(surface, baseUV + offset);
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
                    vec4 c = texture(surface, baseUV + offset);
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
            switch((strength < 0) ? blurStrength : strength) {
                case 3:
                    return GaussBlur7x7(baseUV, (spread < 0) ? blurSpread : spread);
                case 2:
                    return GaussBlur5x5(baseUV, (spread < 0) ? blurSpread : spread);
                case 1:
                    return GaussBlur3x3(baseUV, (spread < 0) ? blurSpread : spread);
                default:
                    return texture(surface, baseUV);
            }
        }
      )"
    );
    return source;
};


const String& CelShadingFuncs() {
    static const String source(R"(
        float CelPeak(vec3 light) {
            return max(light.r, max(light.g, light.b));
        }

        float CelQuantize(float value, int bands) {
            float f = value * float(bands);
            float e = clamp(fwidth(f), 0.001, 0.5);
            return (floor(f) + smoothstep(1.0 - e, 1.0, fract(f))) / float(bands);
        }

        vec3 CelLight(vec3 light, int bands) {
            if (bands <= 0)
                return light;
            float peak = CelPeak(light);
            if (peak <= 0.0)
                return light;
            return light * (CelQuantize(min(peak, 1.0), bands) / peak);
        }

        float RimLight(vec3 normal, vec3 viewDir, float power, float strength) {
            float rim = 1.0 - clamp(dot(normalize(normal), normalize(viewDir)), 0.0, 1.0);
            return strength * pow(rim, power);
        }

        vec3 CelShade(vec3 light, vec3 normal, vec3 viewDir, int bands, float rimPower, float rimStrength) {
            return CelLight(light + vec3(RimLight(normal, viewDir, rimPower, rimStrength)), bands);
        }

        float CelQuantizeRound(float value, int levels) {
            float f = value * float(levels);
            float e = clamp(fwidth(f), 0.05, 0.5);
            return (floor(f) + smoothstep(0.5 - e, 0.5 + e, fract(f))) / float(levels);
        }

        vec3 CelAlbedo(vec3 color, int levels) {
            if (levels <= 0)
                return color;
            float peak = CelPeak(color);
            if (peak <= 0.0)
                return color;
            return color * (CelQuantizeRound(min(peak, 1.0), levels) / peak);
        }
    )");
    return source;
}


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
        float hash13(vec3 p) {
            return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
        }
        float valueNoise3D(vec3 x) {
            vec3 i = floor(x);
            vec3 f = fract(x);
            f = f*f*(3.0 - 2.0*f);
            float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
            float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
            float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
            float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
            float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
            float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
            float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
            float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
            float n00 = mix(n000, n100, f.x);
            float n10 = mix(n010, n110, f.x);
            float n01 = mix(n001, n101, f.x);
            float n11 = mix(n011, n111, f.x);
            return mix(mix(n00, n10, f.y), mix(n01, n11, f.y), f.z);
        }
        float fbm3D(vec3 x, int octaves) {
            float sum = 0.0;
            float amp = 0.5;
            float norm = 0.0;
            for (int i = 0; i < octaves; i++) {
                sum += valueNoise3D(x) * amp;
                norm += amp;
                x = x * 2.03 + 17.1;
                amp *= 0.5;
            }
            return sum / max(norm, 1.0e-5); // [0..1]
        }
        const float simplexSkew = 1.0 / 3.0;
        const float simplexUnskew = 1.0 / 6.0;
        const float simplexRadius2 = 0.6;
        const float simplexScale = 52.0;
        vec3 gradientHash3D(vec3 p) {
            p = vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)), dot(p, vec3(113.5, 271.9, 124.6)));
            return fract(sin(p) * 43758.5453) * 2.0 - 1.0;
        }
        float simplexNoise3D(vec3 x) {
            vec3 s = floor(x + (x.x + x.y + x.z) * simplexSkew);
            vec3 x0 = x - s + (s.x + s.y + s.z) * simplexUnskew;
            vec3 g = step(x0.yzx, x0);
            vec3 l = 1.0 - g;
            vec3 i1 = min(g, l.zxy);
            vec3 i2 = max(g, l.zxy);
            vec3 x1 = x0 - i1 + simplexUnskew;
            vec3 x2 = x0 - i2 + 2.0 * simplexUnskew;
            vec3 x3 = x0 - 1.0 + 3.0 * simplexUnskew;
            vec4 w = max(simplexRadius2 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
            vec4 d = vec4(dot(gradientHash3D(s), x0), dot(gradientHash3D(s + i1), x1), dot(gradientHash3D(s + i2), x2), dot(gradientHash3D(s + 1.0), x3));
            w *= w;
            w *= w;
            return dot(w, d) * simplexScale;
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
            float rC = texture(surface, baseUV + offset).r;
            float gC = texture(surface, baseUV).g;
            float bC = texture(surface, baseUV - offset).b;
            return vec3(rC, gC, bC);
        }

        // Hybrid CA (Delta-Fringe): add only the RB fringe to the already-processed baseColor
        // Keeps your blur/tint/gray intact and costs only 2 extra fetches.
        vec3 ChromaticAberration(vec2 baseUV, vec2 dispUV, vec3 baseColor) {
            if (aberration < 1e-6)
                return baseColor;
            vec2 offset = (offsetType == 1) ? RadialOffset(baseUV) : LinearOffset(dispUV);
            vec3 c0 = texture(surface, baseUV).rgb;
            float rS = texture(surface, baseUV + offset).r;
            float bS = texture(surface, baseUV - offset).b;
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
