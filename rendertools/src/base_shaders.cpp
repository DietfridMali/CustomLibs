
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& PlainColorShader() {
    static const ShaderSource plainColorShader(
        "plainColor",
        StandardVS(),
        R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform vec4 surfaceColor;
        out vec4 fragColor;
        void main() { fragColor = surfaceColor; }
        )"
        );
    return plainColorShader;
}


const ShaderSource& GrayScaleShader() {
    static const ShaderSource grayScaleShader(
        "grayScale",
        StandardVS(),
        R"(
        #version 330 core
        // Für OpenGL ES 3.0 statt dessen:
        // #version 300 es
        // precision mediump float;

        uniform sampler2D source;
        in vec2 fragTexCoord;
        out vec4 fragColor;

        void main() {
            vec4 texColor = texture(source, fragTexCoord);
            // Rec.601 Luminanzgewichte in Gamma-Space
            float gray = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            fragColor = vec4(vec3(gray), texColor.a);
        }
        )" 
        );
    return grayScaleShader;
}


// render a b/w mask with color applied.
const ShaderSource& PlainTextureShader() {
    static const ShaderSource plainTextureShader(
        "plainTexture",
        StandardVS(),
        R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform sampler2D source;
        uniform vec4 surfaceColor;
        in vec3 fragPos;
        in vec2 fragTexCoord;

        layout(location = 0) out vec4 fragColor;
        
        void main() {
            vec4 texColor = texture (source, fragTexCoord);
            float a = texColor.a * surfaceColor.a;
            if (a == 0) discard;
            fragColor = vec4 (texColor.rgb * surfaceColor.rgb, a);
            }
    )"
    );
    return plainTextureShader;
}

// =================================================================================================
