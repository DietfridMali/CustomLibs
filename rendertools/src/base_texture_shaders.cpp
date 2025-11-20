
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& DepthShader() {
    static const ShaderSource source(
        "depthShader",
        R"(
        #version 330
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 texCoord;
        uniform mat4 mModelView;
        uniform mat4 mShadowTransform;
        out vec2 fragCoord;
        void main() {
            gl_Position = mShadowTransform * mModelView * vec4(position, 1.0);
            fragCoord = texCoord;
        }
        )",
        R"(
        #version 330 core
        uniform sampler2D surface;
        uniform vec4 surfaceColor;
        in vec2 fragCoord;
        void main() { 
#if 1
            if (texture(surface, fract(fragCoord)).a * surfaceColor.a < 0.9)
                discard;
#endif
            }
        )"
        );
    return source;
}


const ShaderSource& ActorDepthShader() {
    static const ShaderSource source(
        "actorDepthShader",
        R"(
        #version 330
        layout(location = 0) in vec3 vertex;
        uniform mat4 mModelView;
        uniform mat4 mShadowTransform;

#define CULL_FACES 1

#if CULL_FACES
        uniform mat4 mModelRotation;
        out vec3 worldNormal;
#endif
        void main() {
            gl_Position = mShadowTransform * mModelView * vec4(vertex, 1.0);
#if CULL_FACES
            worldNormal = mat3(mModelRotation) * vertex;
#endif
            }
        )",
        R"(
        #version 330 core

        uniform vec3 lightDir;
#define CULL_FACES 1

#if CULL_FACES
        in vec3 worldNormal;
#endif
        void main() { 
#if CULL_FACES
            if (dot(worldNormal, lightDir) >= 0.0)
                discard;
#endif
            }
        )"
    );
    return source;
}


const ShaderSource& DepthRenderer() {
    static const ShaderSource source(
        "depthRenderer",
        Standard2DVS(),
        R"(
        #version 330 core
        uniform sampler2D surface;
        in vec2 fragCoord;
        out vec4 fragColor;
        void main() { 
            float d = texture(surface, vec2(fragCoord.x, 1.0 - fragCoord.y)).r;
            d = pow(d, 0.5);
            fragColor = vec4(d, d, d, 1.0f);
        }
        )"
    );
    return source;
}


const ShaderSource& PlainColorShader() {
    static const ShaderSource source(
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
    return source;
}


const ShaderSource& GrayScaleShader() {
    static const ShaderSource source(
        "grayScale",
        Standard2DVS(),
        R"(
        #version 330 core
        // Für OpenGL ES 3.0 statt dessen:
        // #version 300 es
        // precision mediump float;

        uniform sampler2D surface;
        uniform vec2 tcOffset;
        uniform vec2 tcScale;
        uniform float brightness;
        in vec2 fragCoord;
        out vec4 fragColor;
        void main() {
            vec4 texColor = texture(surface, tcOffset + fragCoord * tcScale);
            // Rec.601 Luminanzgewichte in Gamma-Space
            float gray = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            gray *= brightness;
            fragColor = vec4(vec3(gray), texColor.a);
        }
        )");
    return source;
}


// render a b/w mask with color applied.
const ShaderSource& PlainTextureShader() {
    static const ShaderSource source(
        "plainTexture",
        Standard2DVS(),
        R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform sampler2D surface;
        uniform vec4 surfaceColor;
        uniform vec2 tcOffset;
        uniform vec2 tcScale;
        //uniform float premultiply;
        in vec3 fragPos;
        in vec2 fragCoord;

        layout(location = 0) out vec4 fragColor;
        
        void main() {
            vec4 texColor = texture(surface, tcOffset + (fract(fragCoord)) * tcScale);
            float a = texColor.a * surfaceColor.a;
            if (a == 0) discard;
            fragColor = vec4 (texColor.rgb * surfaceColor.rgb /** mix (1.0, a, premultiply)*/, a);
            }
        )");
    return source;
}


const ShaderSource& MovingTextureShader() {
    static const ShaderSource source(
        "movingTexture",
        Standard2DVS(),
        String(R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform sampler2D surface;
        uniform vec4 surfaceColor;
        uniform vec2 direction;
        uniform float speed;
        uniform float time;
        //uniform float premultiply;
        in vec3 fragPos;
        in vec2 fragCoord;

        layout(location = 0) out vec4 fragColor;

        // Konstanten
        const vec3  LUMA          = vec3(0.2126, 0.7152, 0.0722);
        const float THRESHOLD     = 0.6;   // 0..1 (linear space)
        const float GAMMA_BRIGHT  = 2.2;   // >1 => stärkeres Aufhellen über Threshold
        const float GAMMA_DARK    = 1.5;   // >1 => stärkeres Abdunkeln unter Threshold
        )") +
        BoostFuncs() +
        String(R"(
        void main() {
#if 0
            fragColor = vec4(1,0,1,1);
#else            
            vec4 texColor = texture(surface, fragCoord + direction * (time * speed));
            float a = texColor.a * surfaceColor.a;
            //if (a == 0) discard;

            vec3 rgbColor = texColor.rgb * surfaceColor.rgb;
#   if 1
            fragColor = vec4(SmoothBoost(rgbColor, 2.0), 1.0); 
#   else
            float L = dot(rgbColor, LUMA);
            float r = L / max(THRESHOLD, 1e-6);

            float L2 = (r < 1.0)
                        ? THRESHOLD * pow(r, GAMMA_DARK)
                        : THRESHOLD * pow(r, 1.0 / GAMMA_BRIGHT);

            float s = (L > 0.0) ? (L2 / L) : 0.0;
            fragColor = vec4(rgbColor * s, a); 
            //fragColor = vec4 (texColor.rgb * surfaceColor.rgb /** mix (1.0, a, premultiply)*/, a);
#   endif
#endif
            }
        )")
    );
    return source;
}


// render a b/w mask with color applied.
const ShaderSource& BlurTextureShader() {
    static const ShaderSource source(
        "blurTexture",
        Standard2DVS(),
        String(R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform sampler2D surface;
        uniform vec4 surfaceColor;
        //uniform float premultiply;
        in vec3 fragPos;
        in vec2 fragCoord;
        )")
        + GaussBlurFuncs() +
        String(R"(
        layout(location = 0) out vec4 fragColor;
        void main() {
            vec4 texColor = GaussBlur(fragCoord, -1, -1);
            float a = texColor.a * surfaceColor.a;
            if (a == 0) discard;
            fragColor = vec4 (texColor.rgb * surfaceColor.rgb /** mix (1.0, a, premultiply)*/, a);
            }
        )")
    );
    return source;
}


const ShaderSource& TintAndBlurShader() {
    static const ShaderSource source(
        "tintAndBlur",
        Standard2DVS(),
        String(R"(
            #version 330 core
            // Für OpenGL ES 3.0 statt dessen:
            // #version 300 es
            // precision mediump float;

            uniform sampler2D surface;
            in vec2 fragCoord;
            out vec4 fragColor;
            uniform float brightness;
            uniform float contrast;     // Kontrastfaktor (>1 = mehr Kontrast, z.B. 1.5)
            uniform float gamma;        // Gammawert (z.B. 2.2 für Standard-Monitor)
            uniform vec4 tint;
        )") +
        GaussBlurFuncs() +
        TintFuncs() +
        String(R"(
        void main() {
            vec2 baseUV = fragCoord;
            vec4 texColor = GaussBlur(baseUV, -1, -1);
            // Rec.601 Luminanzgewichte in Gamma-Space
            float gray = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            gray = (gray - 0.5) * contrast + 0.5;
            gray = clamp(gray, 0.0, 1.0);
            gray = pow(gray, 1.0 / gamma);
            gray *= brightness;
            vec3 finalRGB = mix(vec3(gray), tint.rgb * gray, tint.a);
            fragColor = vec4(finalRGB, texColor.a);
        }
        )")
    );
    return source;
}

// =================================================================================================
