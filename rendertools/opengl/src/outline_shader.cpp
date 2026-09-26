
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
            uniform vec2 texelSize;
            //uniform float premultiply;
            void main() {
                vec4 color = texture(surface, fragCoord);
                if (color.a > 0.0) {
                    fragColor = vec4(mix (outlineColor.rgb, color.rgb, color.a), 1);
                    return;
                }
                float alpha = 0.0;
                int r = int(ceil(outlineWidth));
                for (int y = -r; y <= r; y++) {
                    for (int x = -r; x <= r; x++) {
                        float weight = clamp(outlineWidth + 0.5 - length(vec2(float(x), float(y))), 0.0, 1.0);
                        if (weight > 0.0)
                            alpha = max(alpha, weight * texture(surface, fragCoord + vec2(float(x), float(y)) * texelSize).a);
                        }
                    }
                fragColor = (alpha > 0.0) ? vec4(outlineColor.rgb /** mix(1.0, alpha, premultiply)*/, alpha) : vec4(0.0);
                }
            )"
    );
    return outlineShader;
}

// =================================================================================================
