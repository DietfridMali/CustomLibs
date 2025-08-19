
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
            if (texColor.a == 0) discard;
            fragColor = vec4 (texColor.rgb * surfaceColor.rgb, texColor.a * surfaceColor.a);
            }
    )"
    );
    return plainTextureShader;
}

// =================================================================================================
