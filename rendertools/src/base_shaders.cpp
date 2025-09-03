
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& DepthShader() {
    static const ShaderSource shader(
        "depthShader",
        R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 texCoord;
        uniform mat4 mModelView;
        uniform mat4 mProjection;
        void main() { 
            gl_Position = mProjection * mModelView * vec4 (position, 1.0);
        }
        )",
        R"(
        void main() { }
        )"
        );
    return shader;
}


const ShaderSource& PlainColorShader() {
    static const ShaderSource shader(
        "plainColor",
        Standard2DVS(),
        R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform vec4 surfaceColor;
        out vec4 fragColor;
        void main() { 
            fragColor = surfaceColor; 
        }
        )"
        );
    return shader;
}


const ShaderSource& GrayScaleShader() {
    static const ShaderSource shader(
        "grayScale",
        Standard2DVS(),
        R"(
        #version 330 core
        // Für OpenGL ES 3.0 statt dessen:
        // #version 300 es
        // precision mediump float;

        uniform sampler2D source;
        uniform float brightness;
        in vec2 fragTexCoord;
        out vec4 fragColor;
        void main() {
            vec4 texColor = texture(source, fragTexCoord);
            // Rec.601 Luminanzgewichte in Gamma-Space
            float gray = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            gray *= brightness;
            fragColor = vec4(vec3(gray), texColor.a);
        }
        )" 
        );
    return shader;
}


// render a b/w mask with color applied.
const ShaderSource& PlainTextureShader() {
    static const ShaderSource shader(
        "plainTexture",
        Standard2DVS(),
        R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform sampler2D source;
        uniform vec4 surfaceColor;
        uniform vec2 tcOffset;
        uniform vec2 tcScale;
        //uniform float premultiply;
        in vec3 fragPos;
        in vec2 fragTexCoord;

        layout(location = 0) out vec4 fragColor;
        
        void main() {
            vec4 texColor = texture (source, tcOffset + fragTexCoord * tcScale);
            float a = texColor.a * surfaceColor.a;
            if (a == 0) discard;
            fragColor = vec4 (texColor.rgb * surfaceColor.rgb /** mix (1.0, a, premultiply)*/, a);
            }
    )"
    );
    return shader;
}


const ShaderSource& TintAndBlurShader() {
    static const ShaderSource shader(
        "tintAndBlur",
        Standard2DVS(),
        R"(
        #version 330 core
        // Für OpenGL ES 3.0 statt dessen:
        // #version 300 es
        // precision mediump float;

        uniform sampler2D source;
        in vec2 fragTexCoord;
        out vec4 fragColor;
        uniform float brightness;
        uniform float contrast;     // Kontrastfaktor (>1 = mehr Kontrast, z.B. 1.5)
        uniform float gamma;        // Gammawert (z.B. 2.2 für Standard-Monitor)
        uniform vec4 tint;
        uniform int blurRadius;
        uniform vec2 texelSize;

        vec4 GaussBlur7x7(float spread)
        {
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


        void main() {
            vec4 texColor = (blurRadius > 2) ? GaussBlur7x7(3) : (blurRadius == 2) ? GaussBlur5x5(3) : (blurRadius == 1) ? GaussBlur3x3(3) : texture(source, fragTexCoord);
            // Rec.601 Luminanzgewichte in Gamma-Space
            float gray = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            gray = (gray - 0.5) * contrast + 0.5;
            gray = clamp(gray, 0.0, 1.0);
            gray = pow(gray, 1.0 / gamma);
            gray *= brightness;
            vec3 finalRGB = mix(vec3(gray), tint.rgb * gray, tint.a);
            fragColor = vec4(finalRGB, texColor.a);
        }
        )"
    );
    return shader;
}

// =================================================================================================
