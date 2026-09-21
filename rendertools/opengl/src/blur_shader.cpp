
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& BoxBlurShader() {
    static const ShaderSource boxBlurShader(
        "boxblur",
        Offset2DVS(),
        String(R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            uniform sampler2D surface;
            float FXAA_SPAN_MAX = 16.0;
            float FXAA_REDUCE_MIN = 1.0 / 128.0;
            float FXAA_REDUCE_MUL = 1.0 / 8.0;
            //uniform float premultiply;
            in vec2 fragCoord;
            out vec4 fragColor;
            )") +
            GaussBlurFuncs() +
            String(R"(
#if 0
            vec3 FxaaPixelShader(vec2 pos, sampler2D tex, vec2 texelSize) {
                vec3 rgbNW = textureOffset(tex, pos, ivec2(-1, -1)).xyz;
                vec3 rgbNE = textureOffset(tex, pos, ivec2(1, -1)).xyz;
                vec3 rgbSW = textureOffset(tex, pos, ivec2(-1, 1)).xyz;
                vec3 rgbSE = textureOffset(tex, pos, ivec2(1, 1)).xyz;
                vec3 rgbM = textureLod(tex, pos, 0.0).xyz;
                const vec3 luma = vec3(0.299, 0.587, 0.114);
                float lumaNW = dot(rgbNW, luma);
                float lumaNE = dot(rgbNE, luma);
                float lumaSW = dot(rgbSW, luma);
                float lumaSE = dot(rgbSE, luma);
                float lumaM = dot(rgbM, luma);
                float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
                float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
                vec2 dir = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
                float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
                float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
                dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * texelSize;
                vec3 rgbA = 0.5 * (textureLod(tex, pos + dir * (1.0 / 3.0 - 0.5), 0.0).xyz + textureLod(tex, pos + dir * (2.0 / 3.0 - 0.5), 0.0).xyz);
                vec3 rgbB = rgbA * 0.5 + 0.25 * (textureLod(tex, pos + dir * -0.5, 0.0).xyz + textureLod(tex, pos + dir * 0.5, 0.0).xyz);
                float lumaB = dot(rgbB, luma);
                return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
                }
#endif
            void main() {
#if 1
                fragColor = GaussBlur(fragCoord, 3, 1);
#else
                //vec2 texelSize = 1.0 / vec2(textureSize(surface, 0));
                //vec3 color = FxaaPixelShader(fragCoord, surface, texelSize);
                float a = texture(surface, fragCoord).a;
                fragColor = vec4(color /** mix(1.0, a, premultiply)*/, a);
#endif
                }
            )")
    );
    return boxBlurShader;
}

const ShaderSource& FxaaShader() {
    static const ShaderSource fxaaShader(
        "fxaa",
        Offset2DVS(),
        R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform sampler2D surface;
        float FXAA_SPAN_MAX = 16.0;
        float FXAA_REDUCE_MIN = 1.0 / 128.0;
        float FXAA_REDUCE_MUL = 1.0 / 8.0;
        uniform vec2 texelSize;
        //uniform float premultiply;
        in vec2 fragCoord;
        out vec4 fragColor;
        vec3 FxaaPixelShader(vec2 pos, sampler2D tex, vec2 texelSize) {
            vec3 rgbNW = textureOffset(tex, pos, ivec2(-1, -1)).xyz;
            vec3 rgbNE = textureOffset(tex, pos, ivec2(1, -1)).xyz;
            vec3 rgbSW = textureOffset(tex, pos, ivec2(-1, 1)).xyz;
            vec3 rgbSE = textureOffset(tex, pos, ivec2(1, 1)).xyz;
            vec3 rgbM = textureLod(tex, pos, 0.0).xyz;
            const vec3 luma = vec3(0.299, 0.587, 0.114);
            float lumaNW = dot(rgbNW, luma);
            float lumaNE = dot(rgbNE, luma);
            float lumaSW = dot(rgbSW, luma);
            float lumaSE = dot(rgbSE, luma);
            float lumaM = dot(rgbM, luma);
            float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
            float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
            vec2 dir = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
            float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
            float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
            dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * texelSize;
            vec3 rgbA = 0.5 * (textureLod(tex, pos + dir * (1.0 / 3.0 - 0.5), 0.0).xyz + textureLod(tex, pos + dir * (2.0 / 3.0 - 0.5), 0.0).xyz);
            vec3 rgbB = rgbA * 0.5 + 0.25 * (textureLod(tex, pos + dir * -0.5, 0.0).xyz + textureLod(tex, pos + dir * 0.5, 0.0).xyz);
            float lumaB = dot(rgbB, luma);
            return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
        }
        void main() {
            vec3 color = FxaaPixelShader(fragCoord, surface, texelSize);
            float a = texture(surface, fragCoord).a;
            fragColor = vec4(color /** mix(1.0, a, premultiply)*/, a);
        }
        )"
    );
    return fxaaShader;
}

const ShaderSource& GaussBlurShader() {
    static const ShaderSource gaussBlurShader(
        "gaussblur",
        Offset2DVS(),
        R"(
        //#version 140
        //#extension GL_ARB_explicit_attrib_location : enable
        #version 330
        uniform sampler2D surface;
        uniform float direction;
        in vec2 fragCoord;
        out vec4 fragColor;
        uniform int radius;
        uniform float coeffs[33];
        uniform vec2 texelSize;
        //uniform float premultiply;
        void main() {
#if 0
            fragColor = vec4(0,1,0,1);
#else
            vec2 offset = vec2 (1.0 - direction, direction);
    	    vec4 sum = vec4(0.0);
            int n = 2 * radius + 1;
    	    for (int i = 0; i < n; ++i)	{
    		    vec2 coord = fragCoord + offset * float(i - radius) * texelSize;
    		    sum += vec4 (coeffs[i] * texture(surface, coord));
    	    }
    	    fragColor = vec4(sum.rgb /** mix(1.0, sum.a, premultiply)*/, sum.a);
#endif
        }
        )"
    );
    return gaussBlurShader;
}

// =================================================================================================

// -------------------------------------------------------------------------------------------------
// The bilateral (edge stopping) blur. What tells it apart from the gauss above is that the image it
// filters is not a picture but a value belonging to the SURFACE behind each pixel - an ambient
// occlusion term, a shadow mask. Filtered flat, such a value walks across every silhouette in the
// frame and leaves a halo along it. So each tap is weighted by how much its surface agrees with the
// centre pixel's: by the angle between the two normals, and by how far apart the two surfaces are.
//
// distanceSource is the one thing the two users disagree about, and it is a uniform rather than a
// second shader: with a world position G-buffer the distance is the neighbour's offset from the centre
// pixel's TANGENT PLANE (a floor running to the horizon stays ~0 and is kept, only real steps are
// rejected), without one it is the difference of the linearized scene depths. Everything else - the
// normals, the spatial gauss, the early out where a pixel names no surface - is shared.
//
// projDepth carries (A, B) of the projection matrix, so the depth path needs neither the near nor the
// far plane: the eye space distance of a normalized device z is B / (z + A).

const ShaderSource& BilateralBlurShader() {
    static const ShaderSource source(
        "bilateralBlur",
        String(R"(
            #version 330
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 texCoord;

            uniform mat4 mModelView;
            uniform mat4 mProjection;

            out vec2 fragCoord;

            void main() {
                fragCoord = texCoord;
                gl_Position = vec4(position, 1.0);
                vec4 viewPos = mModelView * vec4(position, 1.0);
                gl_Position = mProjection * viewPos;
            }
        )"),
        String(R"(
            #version 330
            in vec2 fragCoord;
            out vec4 fragColor;

            uniform sampler2D surface;
            uniform sampler2D uWorldNormals;
            uniform sampler2D uWorldPositions;
            uniform sampler2D uSceneDepth;
            uniform vec2 texelSize;
            uniform float direction;
            uniform int radius;
            uniform float normalPower;
            uniform float posSigma;
            uniform int distanceSource;     // 0 = world positions, 1 = scene depth
            uniform vec2 projDepth;         // (A, B): eye distance = B / (ndc z + A)
            uniform bool flipVertically;

            float EyeDepth(float d) {
                return abs(projDepth.y / (2.0 * d - 1.0 + projDepth.x));
            }

            void main() {
                // flipVertically = false: the filtered image, the normals and the depth all share one
                // orientation - they were rasterized by the same pipeline - and are read at the same uv.
                // true is the legacy orientation split of the SSAO path, where the G-buffer is read
                // flipped in the first pass and raw in the second.
                vec2 muv = fragCoord;                                                          // filtered image / output
                vec2 uv  = flipVertically ? vec2(fragCoord.x, 1.0 - fragCoord.y) : fragCoord;  // G-buffer
                vec4 nrmC = textureLod(uWorldNormals, uv, 0.0);
                if (dot(nrmC.xyz, nrmC.xyz) < 0.25) {
                    fragColor = textureLod(surface, muv, 0.0);
                    return;
                }
                vec3 NC = normalize(nrmC.xyz);
                vec3 PC = (distanceSource == 0) ? textureLod(uWorldPositions, uv, 0.0).xyz : vec3(0.0);
                float DC = (distanceSource == 0) ? 0.0 : EyeDepth(textureLod(uSceneDepth, uv, 0.0).r);
                vec2 dirStep = (direction < 0.5) ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);
                float sigmaS = max(float(radius) * 0.5, 1.0);
                vec4 sum = vec4(0.0);
                float sumW = 0.0;
                for (int i = -radius; i <= radius; ++i) {
                    vec2 suv  = uv  + dirStep * float(i);   // G-buffer tap
                    vec2 smuv = muv + dirStep * float(i);   // image tap, same screen pixel as suv
                    vec4 nrmS = textureLod(uWorldNormals, suv, 0.0);
                    if (dot(nrmS.xyz, nrmS.xyz) < 0.25)
                        continue;
                    vec3 NS = normalize(nrmS.xyz);
                    float wS = exp(-float(i * i) / (2.0 * sigmaS * sigmaS));
                    float wN = pow(max(dot(NC, NS), 0.0), normalPower);
                    float dP;
                    if (distanceSource == 0) {
                        // Plane distance, NOT Euclidean: how far the neighbour lies off the centre
                        // pixel's tangent plane. A grazing same-surface (floor running to the horizon)
                        // stays ~0 and is kept; only real depth steps are rejected. The old
                        // length(PC-PS) blew up on grazing surfaces (screen-adjacent pixels sit far
                        // apart in world space) -> every tap rejected -> no blur -> raw noise survived.
                        vec3 PS = textureLod(uWorldPositions, suv, 0.0).xyz;
                        dP = abs(dot(PS - PC, NC));
                    }
                    else
                        dP = abs(EyeDepth(textureLod(uSceneDepth, suv, 0.0).r) - DC);
                    float wP = exp(-(dP * dP) / (2.0 * posSigma * posSigma));
                    float w = wS * wN * wP;
                    sum += textureLod(surface, smuv, 0.0) * w;
                    sumW += w;
                }
                fragColor = (sumW > 0.0) ? sum / sumW : textureLod(surface, muv, 0.0);
            }
        )")
        );
    return source;
};


// The name the scene depth user deploys (see the note in src/common-hlsl/blur_shader.cpp - HLSL needs
// one shader per distance source). OpenGL does not validate a sampler the branch never reads, so here
// it is the same program under the second name.
const ShaderSource& BilateralBlurDepthShader() {
    static const ShaderSource source("bilateralBlurDepth", BilateralBlurShader().m_vs, BilateralBlurShader().m_fs);
    return source;
}

// =================================================================================================
