
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const ShaderSource& DepthShader() {
    static const ShaderSource source(
        "depthShader",
        R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 texCoord;
        uniform mat4 mModelView;
        uniform mat4 mProjection;
        void main() { 
            gl_Position = mProjection * mModelView * vec4 (position, 1.0);
        }
        )",
        R"(
        void main() { }
        )"
        );
    return source;
}


const ShaderSource& LineShader() {
    static const ShaderSource source(
        "lineShader",
        Standard2DVS(),
        R"(
            #version 330 core

            uniform vec2 start;
            uniform vec2 end;
            uniform float strength;      // Pixel
            uniform vec4 surfaceColor;
            // optional, billigstes AA
            uniform bool antialias;      // false = harte Kante

            in vec2 fragCoord;           // [0..viewportSize]
            out vec4 fragColor;

            void main() {
                vec2 p = fragCoord;
                vec2 v = end - start;
                float len2 = dot(v, v);
                float r = 0.5 * max(strength, 0.0);

                float d; // signed distance: <0 innen
                if (len2 < 1e-6) {
                    d = length(p - start) - r;                  // runder Punkt
                } 
                else {
                    vec2 w = p - start;
                    float t = clamp(dot(w, v) / len2, 0.0, 1.0);
                    vec2 proj = start + t * v;
                    d = length(p - proj) - r;                   // Linie mit runden Kappen
                }

                float alpha;
                if (antialias) {
                    float w = 0.5 * fwidth(d);                  // ~1 Pixel weich
                    alpha = 1.0 - smoothstep(0.0, w, d);
                }
                else {
                    alpha = step(d, 0.0);
                }

                fragColor = vec4(surfaceColor.rgb, surfaceColor.a * alpha);
            }
        )"
        );
    return source;
}


const ShaderSource& RingShader() {
    static const ShaderSource source(
        "ringShader",
        Standard2DVS(),
        R"(
            #version 330 core

            uniform vec2 center;
            uniform float radius;     // äußerer Kreisradius in Pixeln
            uniform float strength;   // Ringdicke in Pixeln
            uniform vec4 surfaceColor;
            uniform bool antialias;   // billigstes AA optional

            in vec2 fragCoord;        // [0..viewportSize]
            out vec4 fragColor;

            void main() {
                vec2 p = fragCoord;
                float dist = length(p - center);
                float innerR, outerR;
                if (radius >= strength) {
                    outerR = radius;
                    innerR = radius - strength;
                } 
                else {
                    outerR = 0.5 * strength;
                    innerR = 0.0;
                }
                float dOuter = dist - outerR;   // <0 innen vom Außenrand
                float dInner = innerR - dist;   // <0 außen vom Innenrand
                float alpha;
                if (antialias) {
                    float w = 0.5 * fwidth(dist);
                    float aOuter = 1.0 - smoothstep(0.0, w, dOuter);
                    float aInner = 1.0 - smoothstep(0.0, w, dInner);
                    alpha = aOuter * aInner;
                } 
                else {
                    alpha = step(dOuter, 0.0) * step(dInner, 0.0);
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

            uniform vec2 center;
            uniform float radius;      // Kreisradius in Pixeln
            uniform vec4 surfaceColor;
            uniform bool antialias;    // optional, billigstes AA

            in vec2 fragCoord;         // [0..viewportSize]
            out vec4 fragColor;

            void main() {
                vec2 p = fragCoord;
                float dist = length(p - center);
                float d = dist - radius;   // <0 = innen
                float alpha;
                if (antialias) {
                    float w = 0.5 * fwidth(dist);          // ~1 Pixel Übergang
                    alpha = 1.0 - smoothstep(0.0, w, d);
                } 
                else {
                    alpha = step(d, 0.0);
                }
                fragColor = vec4(surfaceColor.rgb, surfaceColor.a * alpha);
            }
        )");
    return source;
}

// =================================================================================================
