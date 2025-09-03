
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


// =================================================================================================
