
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& LineShader() {
    static const ShaderSource source(
        "lineShader",
        Standard2DVS(),
        R"(
            #version 330 core

            uniform vec2 viewportSize;   // Pixel
            uniform vec4 surfaceColor;
            uniform vec2 start;          // [0..1] UV
            uniform vec2 end;            // [0..1] UV
            uniform float strength;      // [0..1] UV, bezogen auf min(viewportSize)
            uniform bool antialias;

            in vec2 fragCoord;           // [0..1] UV
            out vec4 fragColor;

            void main() {
                // Umrechnung in Pixel
                vec2 pxFragCoord = fragCoord * viewportSize;
                vec2 pxStart = start * viewportSize;
                vec2 pxEnd = end * viewportSize;

                vec2 v = pxEnd - pxStart;
                float len2 = dot(v, v);

                float pxStrength = strength * min(viewportSize.x, viewportSize.y);
                float r = 0.5 * max(pxStrength, 0.0);

                float d; // signed distance: <0 innen
                if (len2 < 1e-6) {
                    d = length(pxFragCoord - pxStart) - r;                  // Punkt
                } 
                else {
                    vec2 w = pxFragCoord - pxStart;
                    float t = clamp(dot(w, v) / len2, 0.0, 1.0);
                    vec2 proj = pxStart + t * v;
                    d = length(pxFragCoord - proj) - r;                     // Linie mit runden Kappen
                }

                float alpha;
                if (antialias) {
                    float pxWidth = 0.5 * fwidth(d);  // ~1 Pixel weich
                    alpha = 1.0 - smoothstep(0.0, pxWidth, d);
                } 
                else {
                    alpha = step(d, 0.0);
                }

                fragColor = vec4(surfaceColor.rgb, surfaceColor.a * alpha);
            }
            )");
    return source;
}


const ShaderSource& RingShader() {
    static const ShaderSource source(
        "ringShader",
        Standard2DVS(),
        R"(
            #version 330 core

            uniform vec2 center;          // [0..1] UV
            uniform float radius;         // äußerer Kreisradius in Pixeln
            uniform float strength;       // [0..1] UV (Dicke, bezogen auf min(viewportSize))
            uniform vec4 surfaceColor;
            uniform bool antialias;       // billigstes AA optional
            uniform vec2 viewportSize;    // Pixel

            in vec2 fragCoord;            // [0..1] UV
            out vec4 fragColor;

            void main() {
                vec2 pxFragCoord = fragCoord * viewportSize;
                vec2 pxCenter = center * viewportSize;
                float pxDist = length(pxFragCoord - pxCenter);

                float pxStrength = strength * min(viewportSize.x, viewportSize.y);

                float innerR, outerR;
                if (radius >= pxStrength) {
                    outerR = radius;
                    innerR = radius - pxStrength;
                }
                else {
                    outerR = 0.5 * pxStrength; // kleiner Kreis als Punkt
                    innerR = 0.0;
                }

                float dOuter = pxDist - outerR;   // <0 innen vom Außenrand
                float dInner = innerR - pxDist;   // <0 außen vom Innenrand

                float alpha;
                if (antialias) {
                    float pxWidth = 0.5 * fwidth(pxDist);
                    if (dOuter > pxWidth || dInner > pxWidth) 
                        discard;
                    float aOuter = 1.0 - smoothstep(0.0, pxWidth, dOuter);
                    float aInner = 1.0 - smoothstep(0.0, pxWidth, dInner);
                    alpha = aOuter * aInner;
                }
                else {
                    if (dOuter > 0.0 || dInner > 0.0) 
                        discard;
                    alpha = 1.0;
                }

                fragColor = vec4(surfaceColor.rgb, surfaceColor.a * alpha);
            }
        )");
    return source;
}


// render a b/w mask with color applied.
const ShaderSource& CircleShader() {
    static const ShaderSource source(
        "circleShader",
        Standard2DVS(),
        R"(
            #version 330 core
            
            uniform vec2 viewportSize;   // Pixel
            uniform vec4 surfaceColor;
            uniform vec2 center;
            uniform float radius;      // [0..1] in UV
            uniform bool antialias;    // billigstes AA

            in vec2 fragCoord;         // [0..viewportSize]
            out vec4 fragColor;

            void main() {
#if 0
                fragColor = vec4(1,0,1,1);
#else
                vec2 pxDelta = (fragCoord - center) * viewportSize;
                float pxDist = length(pxDelta);
                float pxRadius = radius * min(viewportSize.x, viewportSize.y);
                float d = pxDist - pxRadius;   // <0 = innen

                float alpha;
                if (antialias) {
                    float pxWidth = 0.5 * fwidth(pxDist);  // ~1 Pixel Übergang
                    if (d > pxWidth) discard;
                    alpha = 1.0 - smoothstep(0.0, pxWidth, d);
                } else {
                    if (d > 0.0) discard;
                    alpha = 1.0;
                }

                fragColor = vec4(surfaceColor.rgb, surfaceColor.a * alpha);
#endif
            }
        )");
    return source;
}

// render a b/w mask with color applied.
const ShaderSource& CircleMaskShader() {
    static const ShaderSource source(
        "circleMaskShader",
        Standard2DVS(),
        R"(
            #version 330 core

            uniform sampler2D source;
            uniform vec2 viewportSize;   // Pixel
            uniform vec4 surfaceColor;
            uniform vec4 maskColor;
            uniform vec2 center;
            uniform float radius;      // [0..1] in UV
            uniform float maskScale;
            uniform bool antialias;    // billigstes AA

            in vec2 fragCoord;         // [0..viewportSize]
            out vec4 fragColor;

            void main() {
#if 0
                fragColor = vec4(1,0,1,1);
#else
                vec2 fcDelta = fragCoord - center;
                vec4 mask = texture(source, clamp(center + fcDelta * maskScale, 0.0, 1.0));
                if (mask.a > 0)
                    fragColor = maskColor;
                else {
                    vec2 pxDelta = (fragCoord - center) * viewportSize;
                    float pxDist = length(pxDelta);
                    float pxRadius = radius * min(viewportSize.x, viewportSize.y);
                    float d = pxDist - pxRadius;   // <0 = innen

                    float alpha;
                    if (antialias) {
                        float pxWidth = 0.5 * fwidth(pxDist);  // ~1 Pixel Übergang
                        if (d > pxWidth) 
                            discard;
                        alpha = 1.0 - smoothstep(0.0, pxWidth, d);
                    } 
                    else {
                        if (d > 0.0) 
                            discard;
                        alpha = 1.0;
                    }
                    fragColor = vec4(surfaceColor.rgb, surfaceColor.a * alpha);
                }
#endif
            }
        )");
    return source;
}

// =================================================================================================
