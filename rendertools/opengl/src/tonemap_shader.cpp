#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// Tone mapping: an HDR render target onto the screen.
//
// Basic inventory of any renderer that lights into a floating point target. Without it the frame
// reaches the back buffer unchanged and everything above 1 is simply cut off - which forces the
// clamps back into the lighting itself, where they cost the dynamic lights their effect (a light sum
// that has already reached 1 cannot brighten or tint anything any more).
//
// The curve is the Khronos PBR Neutral tone mapper. Below startCompression it is the identity, so a
// normally lit surface looks exactly as it did; above it the peak channel rolls off towards 1 and the
// colour desaturates as it goes, which is what keeps a bright coloured light from collapsing into a
// white blob. Both constants are the reference values of that curve and are not meant to be tuned -
// what a caller sets is the exposure, i.e. how much of its range it wants to bring into view.

const ShaderSource& ToneMapShader() {
    static const ShaderSource toneMapShader(
        "tonemap",
        Offset2DVS(),
        String(R"(
            #version 330
            uniform sampler2D surface;
            uniform float exposure;
            uniform int bEncodeSRGB;
            uniform vec2 tcOffset;
            uniform vec2 tcScale;
            in vec2 fragCoord;
            out vec4 fragColor;

            const float startCompression = 0.8 - 0.04;
            const float desaturation = 0.15;

            vec3 ToneMap(vec3 c) {
                c *= exposure;
                float peak = max(c.r, max(c.g, c.b));
                if (peak < startCompression)
                    return c;
                float d = 1.0 - startCompression;
                float newPeak = 1.0 - d * d / (peak + d - startCompression);
                c *= newPeak / peak;
                float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
                return mix(c, vec3(newPeak), g);
            }

            vec3 LinearToSRGB(vec3 c) {
                c = clamp(c, 0.0, 1.0);
                return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
            }

            void main() {
                vec4 sceneColor = texture(surface, tcOffset + tcScale * fragCoord);
                vec3 color = ToneMap(sceneColor.rgb);
                if (bEncodeSRGB != 0)
                    color = LinearToSRGB(color);
                fragColor = vec4(color, sceneColor.a);
            }
        )")
    );
    return toneMapShader;
}

// =================================================================================================
