
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
            out vec2 fragTexCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mViewport * mProjection * viewPos;
                fragTexCoord = texCoord;
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
            out vec2 fragTexCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mProjection * viewPos;
                fragTexCoord = texCoord;
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
            out vec2 fragTexCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mViewport * mProjection * vec4(viewPos.x + offset, viewPos.y + offset, viewPos.z, 1.0);
                fragTexCoord = texCoord;
                fragPos = viewPos.xyz;
                }
        )"
    );
    return source;
}


const String& GaussBlurFuncs() {
    static const String source(
        R"(
        uniform int blurStrengh;
        uniform blurSpread;
        uniform vec2 texelSize;

        vec4 GaussBlur7x7(vec2 baseUV) {
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
                    vec2 offset = vec2(float(i), float(j)) * texelSize * blurSpread;
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


        vec4 GaussBlur5x5(vec2 baseUV) {
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
                    vec2 offset = vec2(float(i), float(j)) * texelSize * blurSpread;
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

        vec4 GaussBlur3x3(vec2 baseUV) {
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
                    vec2 offset = vec2(float(i), float(j)) * texelSize * blurSpread;
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

        vec4 GaussBlur(vec2 baseUV) {
            switch(blurStrengh) {
                case 3:
                    return GaussBlur7x7(baseUV);
                case 2:
                    return GaussBlur5x5(baseUV);
                case 1:
                    return GaussBlur3x3(ubaseUV);
                default:
                    return color;
            }
        }
      )"
    );
    return source;
};



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


const String& ChromAbFunc() {
    static const String source(R"(
        // === Chromatic Aberration (UV-space) ===
        // Uses existing uniforms: sampler2D source, vec2 viewportSize
        uniform float aberration;
        uniform int  caType;       // 0 = linear, 1 = radial
        uniform vec2 caLinearDir;  // arbitrary direction for linear CA (need not be normalized)

        // Branchless, safe normalization (returns zero for zero-length input)
        vec2 SafeNorm(vec2 v) {
            float invLen = inversesqrt(max(dot(v, v), 1e-12));
            return v * invLen;
        }

        // Build linear CA offset in UV units
        vec2 LinearUVOffset(vec2 uv) {
            vec2 dir = safeNorm(caLinearDir);
            return dir * aberration; // aberration already in UV units
        }

        // Build radial CA offset in UV units (aspect-correct, circular around center)
        vec2 RadialUVOffset(vec2 uv) {
            float aspect = viewportSize.x / max(viewportSize.y, 1.0);
            vec2 d   = uv - 0.5;                // UV delta from center
            vec2 m   = vec2(d.x * aspect, d.y); // metric space (x scaled by aspect)
            float rM = length(m);               // radial distance in metric space
            vec2 dirM = (rM > 0.0) ? (m / rM) : vec2(0.0); // direction (metric)
            float L   = aberration * rM;        // fringe grows with radius
            // Map metric direction back to UV
            return vec2((L * dirM.x) / max(aspect, 1e-6), L * dirM.y);
        }

        // Dispatcher for offset
        vec2 CAOffset(vec2 uv) {
            return (caType == 1) ? RadialOffsetUV(uv) : LinearOffsetUV(uv);
        }

        // Pure CA: compose RGB from source only (useful in a dedicated CA pass)
        vec3 ChromaticAberration(vec2 uv) {
            vec2 off = CAOffset(uv);
            float rC = texture(source, uv + off).r;
            float gC = texture(source, uv      ).g;
            float bC = texture(source, uv - off).b;
            return vec3(rC, gC, bC);
        }

        // Hybrid CA (Delta-Fringe): add only the RB fringe to the already-processed baseColor
        // Keeps your blur/tint/gray intact and costs only 2 extra fetches.
        vec3 ChromaticAberration(vec2 uv, vec3 baseColor) {
            if (aberration < 1e-6)
                return baseColor;
            vec2 off = BuildCAOffset(uv);
            vec3 c0  = texture(source, uv).rgb;
            float rS = texture(source, uv + off).r;
            float bS = texture(source, uv - off).b;
            vec3 fringe = vec3(rS - c0.r, 0.0, bS - c0.b);
            return baseColor + fringe;
        }
    )");
    return source;
}


// =================================================================================================
