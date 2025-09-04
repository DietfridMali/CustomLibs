
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
        uniform int blurRadius;
        uniform vec2 texelSize;

        vec4 GaussBlur7x7(float spread) {
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
                    vec4 c = texture(source, fragTexCoord + offset);
                    sumRGB += c.rgb * c.a * w; // premultiplied
                    sumA   += c.a * w;
                    wSum   += w;
                }
            }
            vec3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : vec3(0.0);
            float a  = sumA / wSum;
            return vec4(rgb, a);
        }


        vec4 GaussBlur5x5(float spread) {
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
                    vec4 c = texture(source, fragTexCoord + offset);
                    sumRGB += c.rgb * c.a * w; // premultiplied
                    sumA   += c.a * w;
                    wSum   += w;
                }
            }
            vec3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : vec3(0.0);
            float a  = sumA / wSum;
            return vec4(rgb, a);
        }   


        vec4 GaussBlur3x3(float spread) {
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
                    vec4 c = texture(source, fragTexCoord + offset);
                    sumRGB += c.rgb * c.a * w; // premultiplied
                    sumA   += c.a * w;
                    wSum   += w;
                }
            }
            vec3 rgb = (sumA > 1e-6) ? (sumRGB / sumA) : vec3(0.0);
            float a  = sumA / wSum;
            return vec4(rgb, a);
        }
      )"
    );
    return source;
};

const String& TintFuncs() {
    static const String source(R"(
        vec3 ApplyClampedTint(vec3 color, vec3 tintScale) {
            vec3 scale = tintScale - vec3(1.0);
            float maxScale = max(max(scale.r, scale.g), scale.b);
            float maxColor = max(max(color.r, color.g), color.b);
            float minHeadroom = 1.0 - maxColor;
            scale *= min(1.0, minHeadroom / max(maxScale, 1e-6));
            scale += vec3(1.0);
            return clamp(color * scale, 0.0, 1.0);
        }

        // downscale color just so much inf need be that tint can be fully applied
        vec3 ApplyTint(vec3 color, vec3 tintScale) {
            vec3 denom = max(color * tintScale, vec3(1e-6));
            vec3 inv   = 1.0 / denom;
            // t = min(1, min(inv.r, inv.g, inv.b))
            float t = min(1.0, min(inv.r, min(inv.g, inv.b)));
            // erst runterregeln, dann Tint anwenden
            return (color * t) * tintScale;
        }

        vec3 ApplyExponentialTint(vec3 color, vec3 tintScale, float e, float tMin) {
            vec3 s = pow(max(tintScale, vec3(1e-6)), vec3(max(e, 0.0)));
            vec3 denom = max(color * s, vec3(1e-6));
            float t = min(1.0, min(1.0/denom.r, min(1.0/denom.g, 1.0/denom.b)));
            t = max(t, tMin);
            return (color * t) * s;
        }
        )");
    return source;
}

// =================================================================================================
