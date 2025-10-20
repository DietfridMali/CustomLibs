
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

            uniform vec2  center;          // [0..1] UV
            uniform float radius;          // [0..1] UV (außen, bezogen auf min(viewportSize))
            uniform float strength;        // [0..1] UV (Dicke)
            uniform vec4  surfaceColor;
            uniform bool  antialias;
            uniform vec2  viewportSize;    // Pixel

            uniform float startAngle;      // [0..360)
            uniform float endAngle;        // [0..360)

            in vec2 fragCoord;             // [0..1] UV
            out vec4 fragColor;

            const float PI = 3.14159265358979323846;

            float Rad2Deg(float r) { return r * (180.0 / PI); }

            // Winkel mit 0°=12 Uhr, CW steigend
            float Degrees(vec2 d) {
                float r = 0.5 * PI - atan(-d.y, d.x);     
                r -= floor(r / (2.0 * PI)) * (2.0 * PI);
                return Rad2Deg(r);
            }


            void main() {
                float pxScale     = min(viewportSize.x, viewportSize.y);
                vec2  pxDelta     = (fragCoord - center) * viewportSize;
                float pxDist      = length(pxDelta);
                float pxRadius    = radius   * pxScale;
                float pxStrength  = strength * pxScale;

                // Winkel 0..360
                float a = Degrees(pxDelta);
                if (a < 0.0) 
                    a += 360.0;

                // optional Segment (Start..Ende, Ende inkl.)
                bool renderSegment = startAngle != endAngle;
                if (renderSegment && (a < startAngle || a > endAngle))
                    discard;

                float outerR = pxRadius;
                float innerR = max(pxRadius - pxStrength, 0.0);

                float dOuter = pxDist - outerR;   // <0: innen vom Außenrand
                float dInner = innerR - pxDist;   // <0: außen vom Innenrand

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
                    fragColor = mask; //Color;
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


// render a b/w mask with color applied.
const ShaderSource& RectangleShader() {
    static const ShaderSource source(
        "rectangleShader",
        Standard2DVS(),
        R"(
            #version 330 core

            in vec2 fragCoord;         // UV [0..1]
            out vec4 fragColor;

            uniform vec2 viewportSize; // [px]
            uniform vec4 surfaceColor;
            uniform vec2 center;       // [uv]
            uniform vec2 size;         // UV, Rect liegt innerhalb center +/- size  (=> size = Halbgröße)
            uniform float strength;    // relativ zu min(viewportSize)
            uniform float radius;      // relativ zu min(viewportSize)
            uniform bool  antialias;

            vec2  pxFragCoord;
            vec2  pxCenter;
            vec2  pxSize;     // Halbgröße in Pixeln
            float pxRadius;

            // SDF in pixel space to keep thickness and radius pixel perfect and isotropic
            float sdRoundRect() {
                vec2 q = abs(pxFragCoord - pxCenter) - (pxSize - vec2(pxRadius));
                return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - pxRadius; // <0 innen
            }

            void main() {
                // UV -> Pixel (Zuweisung an die globalen px*-Variablen)
                pxFragCoord = fragCoord * viewportSize;
                pxCenter    = center    * viewportSize;
                pxSize      = size      * viewportSize;   // size = half size here
                pxRadius    = clamp(radius * min(viewportSize.x, viewportSize.y),0.0, min(pxSize.x, pxSize.y));

                float sd  = sdRoundRect();                    // distance to outer border [px]
                float aaw = antialias ? fwidth(sd) : 0.0;     // ~1 px

                if (strength <= 0.0) {
                    if (sd > aaw) discard;
                    float alpha = 1.0 - smoothstep(0.0, aaw, sd);
                    fragColor = vec4(surfaceColor.rgb, surfaceColor.a * alpha);
                } 
                else {
                    float pxStrength = strength * min(viewportSize.x, viewportSize.y);
                    float sdi = sd + pxStrength;              // Distanz zur Innenkante

                    // hard discard outside of the aa corridor
                    if (sd > aaw || sdi < -aaw) 
                        discard;

                    // konstante Dicke: Coverages MULTIPLIZIEREN statt min()
                    float covOuter = 1.0 - smoothstep(0.0, aaw, sd);   // Außenkante
                    float covInner =        smoothstep(0.0, aaw, sdi); // Innenkante
                    float alpha    = covOuter * covInner;

                    if (alpha <= 0.0) 
                        discard;
                    fragColor = vec4(surfaceColor.rgb, surfaceColor.a * alpha);
                }
            }
        )");

    return source;
}

// =================================================================================================
