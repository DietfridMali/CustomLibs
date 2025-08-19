
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& OutlineShader() {
    static const ShaderSource outlineShader(
        "outline",
        OffsetVS(),
        R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            in vec2 fragTexCoord;
            out vec4 fragColor;
            uniform sampler2D source;
            uniform vec4 outlineColor;
            uniform float outlineWidth;
            void main() {
                vec4 color = texture(source, fragTexCoord);
                if (color.a > 0.0) {
                    fragColor = vec4(mix (outlineColor.rgb, color.rgb, color.a), 1);
                    return;
                }
                float alpha = 0.0;
                vec2 texelSize = 1.0 / vec2(textureSize(source, 0));
                float dx = outlineWidth * texelSize.x;
                int r = int(outlineWidth);
                for (int x = r; x >= 0; x--, dx -= texelSize.x) {
                    float dy = outlineWidth * texelSize.y;
                    for (int y = r; y >= 0; y--, dy -= texelSize.y) {
                        float a;
                        a = texture(source, fragTexCoord + vec2(-dx, -dy)).a; if (a > alpha) alpha = a;
                        a = texture(source, fragTexCoord + vec2(-dx,  dy)).a; if (a > alpha) alpha = a;
                        a = texture(source, fragTexCoord + vec2( dx,  dy)).a; if (a > alpha) alpha = a;
                        a = texture(source, fragTexCoord + vec2( dx, -dy)).a; if (a > alpha) alpha = a;
                        }
                    }
                fragColor = (alpha > 0.0) ? vec4(outlineColor.rgb, alpha) : vec4(0.0);
                }
            )"
    );
    return outlineShader;
}

// =================================================================================================
