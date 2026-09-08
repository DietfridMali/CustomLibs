#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// Line ribbon shader (OpenGL). Mirrors directx/src/line_shader.cpp. The general purpose line drawer of
// the library (LineRenderer, include/linerenderer.h): one line per instance, pulled from the SSBO
// (binding 0) via gl_InstanceID and expanded from the unit quad into a ribbon in VIEW space, the way
// the lightning ribbon does it. The width is given in TARGET PIXELS and converted into view units at
// the line's depth, so it is exact where glLineWidth was at the driver's mercy - and there is no
// glLineWidth in DX12 at all. The FS draws a capsule distance field (round caps), so joints of a
// strip close without a second pass, and cuts the dash / dot pattern out of it along the line.
//
// Pattern lengths are multiples of the line width w (a dot is the round cap alone, a circle of
// diameter w); dashScale stretches them. Given as CORE lengths, the caps add w/2 at every end:
//   dashed   period 4.5 w: dash core 2 w (3 w visible), gap 1.5 w visible
//   dotted   period 2.5 w: dot core 0     (1 w visible), gap 1.5 w visible
//   dash-dot period 7 w:   dash, gap, dot, gap
// std430 inflates vec3 to 16 bytes, so the record is declared as flat floats to match the 64 byte
// C++ layout (LineRenderer::Line).

static const ShaderDataAttributes LineQuadAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};

static const String LineDrawVS = String(R"(#version 430 core
struct LineInstance {
    float p0x; float p0y; float p0z; float width;    // width in target pixels
    float p1x; float p1y; float p1z; float style;    // 0 solid, 1 dashed, 2 dotted, 3 dash-dot
    float colr; float colg; float colb; float cola;
    float phase; float pad0; float pad1; float pad2; // pattern offset at p0 (view units) - strip continuity
};

layout(std430, binding = 0) buffer Lines { LineInstance lines[]; };

uniform mat4 mModelView;
uniform mat4 mProjection;
uniform mat4 mViewport;
uniform vec2  texelSize;     // one over the TARGET BUFFER size - mClip below is in the buffer's NDC
uniform float perspective;   // 1: pixels per unit shrink with depth; 0: orthographic projection

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;

out vec4 vCap;      // (along, across, segLen, halfWidth) - capsule local, PIXELS
out vec4 vColor;
out vec2 vPattern;  // (style, phase)

void main() {
    LineInstance l = lines[uint(gl_InstanceID)];

    vec3 vp0 = (mModelView * vec4(l.p0x, l.p0y, l.p0z, 1.0)).xyz;
    vec3 vp1 = (mModelView * vec4(l.p1x, l.p1y, l.p1z, 1.0)).xyz;

    // The ribbon is spanned in 3D view space, not in the projection: a line running towards the viewer
    // has no projected length and would collapse there. Across the line it faces the viewer - in view
    // space the eye is the origin; under an orthographic projection it looks down -z.
    vec3 seg = vp1 - vp0;
    float segLen = length(seg);
    vec3 axis = (segLen > 1e-6) ? seg / segLen : vec3(1.0, 0.0, 0.0);
    vec3 toEye = (perspective > 0.5) ? normalize(-0.5 * (vp0 + vp1)) : vec3(0.0, 0.0, 1.0);
    vec3 perp = cross(axis, toEye);
    float perpLen = length(perp);
    // a line seen exactly end on has no unique across direction - any perpendicular will do
    perp = (perpLen > 1e-4) ? perp / perpLen : normalize(cross(axis, vec3(0.0, 0.0, 1.0)));

    // Pixels one view unit covers on screen, ACROSS the line and ALONG it: the projection's derivative
    // along perp and along axis at the line's middle, exact at that depth for a perspective projection
    // and for any orthographic one. The two differ - a 2D ortho over a wide viewport has more pixels
    // per unit in x than in y - so the capsule is handed to the FS in PIXELS. Measured in view units
    // it was an ellipse: the round caps of a horizontal line stuck out 2.7 times further than those of
    // a vertical one on a menu frame, and a width taken from the y scale alone came out 1.78 times too
    // wide on a vertical line.
    mat4 mClip = mViewport * mProjection;
    vec4 cMid = mClip * vec4(0.5 * (vp0 + vp1), 1.0);
    vec4 cPerp = mClip * vec4(perp, 0.0);
    vec4 cAxis = mClip * vec4(axis, 0.0);
    float wMid = (abs(cMid.w) > 1e-6) ? cMid.w : 1e-6;
    vec2 pxScale = 0.5 / texelSize;
    vec2 dPerpNdc = (cPerp.xy - (cMid.xy / wMid) * cPerp.w) / wMid;   // d(ndc) / d(perp)
    vec2 dAxisNdc = (cAxis.xy - (cMid.xy / wMid) * cAxis.w) / wMid;   // d(ndc) / d(axis)
    float pxAcross = max(length(dPerpNdc * pxScale), 1e-6);
    // a line seen nearly end on has no projected length: the along scale is kept above a fraction of
    // the across scale, so the cap's extension in view units stays bounded
    float pxAlong = max(length(dAxisNdc * pxScale), 0.05 * pxAcross);
    float halfWidth = max(0.5 * l.width, 1e-3);   // pixels
    // one pixel of margin around the capsule, so the antialiased edge has the pixels it fades over
    float halfDraw = halfWidth + 1.0;

    // capsule bounding box: x along the line (round cap before p0 .. round cap after p1), y across -
    // spanned in view units, so the pixel margins go back through the scale of their direction
    float capLen = halfDraw / pxAlong;
    float along  = (position.x + 0.5) * (segLen + 2.0 * capLen) - capLen;
    float across = position.y * 2.0 * halfDraw / pxAcross;
    vec3 node = vp0 + axis * along + perp * across;

    gl_Position = mViewport * (mProjection * vec4(node, 1.0));
    vCap = vec4(along * pxAlong, across * pxAcross, segLen * pxAlong, halfWidth);
    vColor = vec4(l.colr, l.colg, l.colb, l.cola);
    vPattern = vec2(l.style, l.phase * pxAlong);
}
)");

static const String LineDrawFS = String(R"(#version 430 core
in  vec4 vCap;
in  vec4 vColor;
in  vec2 vPattern;
out vec4 fragColor;

uniform float dashScale;   // stretches the dash / dot pattern (1 = the lengths above)
uniform float antialias;   // 1: analytic edge antialiasing, 0: hard edge

// distance of t to the interval [s, e] along the line, 0 inside
float IntervalDist(float t, float s, float e) {
    return max(max(s - t, t - e), 0.0);
}

void main() {
    float along  = vCap.x;
    float across = vCap.y;
    float segLen = vCap.z;
    float hw     = vCap.w;

    // the line's own ends: outside [0, segLen] the distance grows and the caps come out round
    float dAlong = IntervalDist(along, 0.0, segLen);

    int style = int(vPattern.x + 0.5);
    if (style != 0) {
        // The pattern in units of the line width. Each "on" piece is a capsule of its own, so the
        // distance to the nearest piece replaces the distance to the whole line - with the previous
        // period looked at too, because a cap reaches across the period boundary.
        float unit = 2.0 * hw * max(dashScale, 1e-3);
        float t = (along + vPattern.y) / unit;
        float period = (style == 1) ? 4.5 : (style == 2) ? 2.5 : 7.0;
        float e0 = (style == 2) ? 0.0 : 2.0;   // first piece: [0, e0] - a dash, or a dot at 0
        float local = t - period * floor(t / period);
        float d = min(IntervalDist(local, 0.0, e0), IntervalDist(local - period, 0.0, e0));
        if (style == 3)   // the dot between the dashes
            d = min(d, min(IntervalDist(local, 4.5, 4.5), IntervalDist(local - period, 4.5, 4.5)));
        dAlong = max(dAlong, d * unit);
    }

    float dist = length(vec2(dAlong, across));
    // The capsule is in pixels, so fwidth (dist) is about one - it still divides, because under a
    // perspective projection the pixel scale drifts along the line. (hw - dist) / fwidth is the pixel
    // distance to the edge; a pixel centred that far inside is covered by that plus a half. Measuring
    // the distance alone gave a line on a pixel boundary 37 % per row where it covers 87 %.
    float fw = max(fwidth(dist), 1e-6);
    float alpha = (antialias > 0.5) ? clamp((hw - dist) / fw + 0.5, 0.0, 1.0) : ((dist <= hw) ? 1.0 : 0.0);
    if (alpha <= 0.0)
        discard;
    fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)");

const ShaderSource& LineDrawShader() {
    static const ShaderSource source(
        "lineDraw",
        LineDrawVS,
        LineDrawFS,
        ShaderDataLayout(LineQuadAttrs, 2)
    );
    return source;
}

// =================================================================================================
