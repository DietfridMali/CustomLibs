
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& OutlineShader() {
    static const ShaderSource outlineShader(
        "outline",
        Offset2DVS(),
        R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            in vec2 fragCoord;
            out vec4 fragColor;
            uniform sampler2D surface;
            uniform vec4 outlineColor;
            uniform float outlineWidth;
            //uniform float premultiply;
            void main() {
                vec4 color = texture(source, fragCoord);
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
                        alpha = max(alpha, texture(source, fragCoord + vec2(-dx, -dy)).a);
                        alpha = max(alpha, texture(source, fragCoord + vec2(-dx,  dy)).a);
                        alpha = max(alpha, texture(source, fragCoord + vec2( dx,  dy)).a);
                        alpha = max(alpha, texture(source, fragCoord + vec2( dx, -dy)).a);
                        }
                    }
                fragColor = (alpha > 0.0) ? vec4(outlineColor.rgb /** mix(1.0, alpha, premultiply)*/, alpha) : vec4(0.0);
                }
            )"
    );
    return outlineShader;
}

// =================================================================================================
