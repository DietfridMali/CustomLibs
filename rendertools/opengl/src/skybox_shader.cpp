#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// This shader lives in rendertools because the draw it belongs to does: Skybox::Render () (src/skybox.cpp)
// asks its shader handler for the id "skybox", and it did so from a source that only Paintjob Rampage
// registered - so anybody else using the class got a null shader and an empty sky. Anything an
// application wants to look different here is a uniform, not a second copy of the source.
//
// The cube is drawn at maximum depth (the xyww trick makes z == w, so the depth test sees 1.0) with
// LEQUAL and no depth write: it fills whatever the scene left at the far plane. mView must be a pure
// rotation about the camera - the caller nulls the translation column.
//
// lightDirection only turns the cube map about Y, so the sun in the texture ends up where the light
// comes from. brightness does two things: it picks the blend of the three cloud cover variants
// (1.0 = sky1, 0.85 = sky2, 0.7 = sky3) and scales the result, which is what makes an overcast sky a
// darker one as well.

const ShaderSource& SkyboxShader() {
    static const ShaderSource source(
        "skybox",
        String(R"(
            #version 330
            layout(location=0) in vec3 position;
            uniform mat4 mProjection;
            uniform mat4 mView;
            out vec3 viewDirection;
            void main() {
                viewDirection = position;
                gl_Position = mProjection * mView * vec4(position,1.0);
                gl_Position = gl_Position.xyww;
            }
        )"),
        String(R"(
            #version 330
            in vec3 viewDirection;
            uniform mat4 mView;
            uniform samplerCube sky1;
            uniform samplerCube sky2;
            uniform samplerCube sky3;
            uniform vec3 lightDirection;
            uniform float brightness;
            uniform float alpha;
            out vec4 fragColor;

            const float PI = 3.141592653589793;

            vec3 YRotate(vec3 v, float a) {
                float c = cos(a);
                float s = sin(a);
                return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
            }

            void main() {
                vec3 viewDir = normalize(viewDirection);
                vec2 h = vec2(lightDirection.x, lightDirection.z);
                float l = length(h);
                float yaw = (l > 1e-5) ? -atan(h.x, h.y) : 0.0;
                vec3 sampleDir = normalize(YRotate(viewDir, yaw));

                // brightness 1.0 -> coverage 0.0 (sky1)
                // brightness 0.85 -> coverage 0.5 (sky2)
                // brightness 0.7  -> coverage 1.0 (sky3)
                float coverage = 1.0 - clamp((brightness - 0.7) / 0.3, 0.0, 1.0);
                vec3 c2 = texture(sky2, sampleDir).rgb;
                vec3 color;

                if (coverage < 0.5) {
                    // [1.0 .. 0.85]: sky1 -> sky2
                    // smoothstep maps coverage [0.0, 0.5] -> a [0.0, 1.0]
                    vec3  c1 = texture(sky1, sampleDir).rgb;
                    float a = smoothstep(0.0, 0.5, coverage);
                    color = mix(c1, c2, a);
                }
                else {
                    // [0.85 .. 0.7]: sky2 -> sky3
                    // smoothstep maps coverage [0.5, 1.0] -> a [0.0, 1.0]
                    vec3  c3 = texture(sky3, sampleDir).rgb;
                    float a = smoothstep(0.5, 1.0, coverage);
                    color = mix(c2, c3, a);
                }

                fragColor = vec4(color * brightness, alpha);
            }
        )")
    );
    return source;
}

// =================================================================================================
