#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// Line ribbon shader (DX). The general purpose line drawer of the library (LineRenderer,
// include/linerenderer.h): one line per instance, pulled from the structured buffer (u0) and expanded
// from the unit quad into a ribbon in VIEW space, the way the lightning ribbon does it. The width is
// given in TARGET PIXELS and converted into view units at the line's depth - DX12 has no line width
// of its own, and OpenGL's was at the driver's mercy. The FS draws a capsule distance field (round
// caps), so joints of a strip close without a second pass, and cuts the dash / dot pattern out of it
// along the line.
//
// Pattern lengths are multiples of the line width w (a dot is the round cap alone, a circle of
// diameter w); dashScale stretches them. Given as CORE lengths, the caps add w/2 at every end:
//   dashed   period 4.5 w: dash core 2 w (3 w visible), gap 1.5 w visible
//   dotted   period 2.5 w: dot core 0     (1 w visible), gap 1.5 w visible
//   dash-dot period 7 w:   dash, gap, dot, gap
// The record matches the 64 byte C++ layout (LineRenderer::Line).

static const ShaderDataAttributes LineQuadAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};

static const String LineDrawVS = String(R"(
struct LineInstance {
    float3 p0;    float width;   // width in target pixels
    float3 p1;    float style;   // 0 solid, 1 dashed, 2 dotted, 3 dash-dot
    float4 color;
    float  phase; float3 pad;    // pattern offset at p0 (view units) - strip continuity
};

RWStructuredBuffer<LineInstance> lines : register(u0);

cbuffer FrameConstants : register(b0) {
    column_major float4x4 mModelView;
    column_major float4x4 mProjection;
    column_major float4x4 mViewport;
};

cbuffer ShaderConstants : register(b1) {   // same block as the PS
    float2 texelSize;      // one over the TARGET BUFFER size - mClip below is in the buffer's NDC
    float  perspective;    // 1: pixels per unit shrink with depth; 0: orthographic projection
    float  dashScale;      // stretches the dash / dot pattern (PS)
    float  antialias;      // 1: analytic edge antialiasing, 0: hard edge (PS)
};

struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float2 tc  : TEXCOORD;
    uint   iid : SV_InstanceID;
};

struct PSInput {
    float4 pos     : SV_Position;
    float4 cap     : TEXCOORD0;   // (along, across, segLen, halfWidth) - capsule local, PIXELS
    float4 color   : TEXCOORD1;
    float2 pattern : TEXCOORD2;   // (style, phase)
};

PSInput VSMain(VSInput v) {
    LineInstance l = lines[v.iid];

    float3 vp0 = mul(mModelView, float4(l.p0, 1.0)).xyz;
    float3 vp1 = mul(mModelView, float4(l.p1, 1.0)).xyz;

    // The ribbon is spanned in 3D view space, not in the projection: a line running towards the viewer
    // has no projected length and would collapse there. Across the line it faces the viewer - in view
    // space the eye is the origin; under an orthographic projection it looks down -z.
    float3 seg = vp1 - vp0;
    float segLen = length(seg);
    float3 axis = (segLen > 1e-6) ? seg / segLen : float3(1.0, 0.0, 0.0);
    float3 toEye = (perspective > 0.5) ? normalize(-0.5 * (vp0 + vp1)) : float3(0.0, 0.0, 1.0);
    float3 perp = cross(axis, toEye);
    float perpLen = length(perp);
    // a line seen exactly end on has no unique across direction - any perpendicular will do
    perp = (perpLen > 1e-4) ? perp / perpLen : normalize(cross(axis, float3(0.0, 0.0, 1.0)));

    // Pixels one view unit covers on screen, ACROSS the line and ALONG it: the projection's derivative
    // along perp and along axis at the line's middle, exact at that depth for a perspective projection
    // and for any orthographic one. The two differ - a 2D ortho over a wide viewport has more pixels
    // per unit in x than in y - so the capsule is handed to the PS in PIXELS. Measured in view units
    // it was an ellipse: the round caps of a horizontal line stuck out 2.7 times further than those of
    // a vertical one on a menu frame, and a width taken from the y scale alone came out 1.78 times too
    // wide on a vertical line.
    float4x4 mClip = mul(mViewport, mProjection);
    float4 cMid = mul(mClip, float4(0.5 * (vp0 + vp1), 1.0));
    float4 cPerp = mul(mClip, float4(perp, 0.0));
    float4 cAxis = mul(mClip, float4(axis, 0.0));
    float wMid = (abs(cMid.w) > 1e-6) ? cMid.w : 1e-6;
    float2 pxScale = 0.5 / texelSize;
    float2 dPerpNdc = (cPerp.xy - (cMid.xy / wMid) * cPerp.w) / wMid;   // d(ndc) / d(perp)
    float2 dAxisNdc = (cAxis.xy - (cMid.xy / wMid) * cAxis.w) / wMid;   // d(ndc) / d(axis)
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
    float along  = (v.pos.x + 0.5) * (segLen + 2.0 * capLen) - capLen;
    float across = v.pos.y * 2.0 * halfDraw / pxAcross;
    float3 node = vp0 + axis * along + perp * across;

    PSInput o;
    o.pos = mul(mViewport, mul(mProjection, float4(node, 1.0)));
    o.cap = float4(along * pxAlong, across * pxAcross, segLen * pxAlong, halfWidth);
    o.color = l.color;
    o.pattern = float2(l.style, l.phase * pxAlong);
    return o;
}
)");

static const String LineDrawFS = String(R"(
struct PSInput {
    float4 pos     : SV_Position;
    float4 cap     : TEXCOORD0;
    float4 color   : TEXCOORD1;
    float2 pattern : TEXCOORD2;
};

cbuffer ShaderConstants : register(b1) {
    float2 texelSize;
    float  perspective;
    float  dashScale;      // stretches the dash / dot pattern (1 = the lengths above)
    float  antialias;      // 1: analytic edge antialiasing, 0: hard edge
};

// distance of t to the interval [s, e] along the line, 0 inside
float IntervalDist(float t, float s, float e) {
    return max(max(s - t, t - e), 0.0);
}

float4 PSMain(PSInput i) : SV_Target {
    float along  = i.cap.x;
    float across = i.cap.y;
    float segLen = i.cap.z;
    float hw     = i.cap.w;

    // the line's own ends: outside [0, segLen] the distance grows and the caps come out round
    float dAlong = IntervalDist(along, 0.0, segLen);

    int style = int(i.pattern.x + 0.5);
    if (style != 0) {
        // The pattern in units of the line width. Each "on" piece is a capsule of its own, so the
        // distance to the nearest piece replaces the distance to the whole line - with the previous
        // period looked at too, because a cap reaches across the period boundary.
        float unit = 2.0 * hw * max(dashScale, 1e-3);
        float t = (along + i.pattern.y) / unit;
        float period = (style == 1) ? 4.5 : (style == 2) ? 2.5 : 7.0;
        float e0 = (style == 2) ? 0.0 : 2.0;   // first piece: [0, e0] - a dash, or a dot at 0
        float local = t - period * floor(t / period);
        float d = min(IntervalDist(local, 0.0, e0), IntervalDist(local - period, 0.0, e0));
        if (style == 3)   // the dot between the dashes
            d = min(d, min(IntervalDist(local, 4.5, 4.5), IntervalDist(local - period, 4.5, 4.5)));
        dAlong = max(dAlong, d * unit);
    }

    float dist = length(float2(dAlong, across));
    // The capsule is in pixels, so fwidth (dist) is about one - it still divides, because under a
    // perspective projection the pixel scale drifts along the line. (hw - dist) / fwidth is the pixel
    // distance to the edge; a pixel centred that far inside is covered by that plus a half. Measuring
    // the distance alone gave a line on a pixel boundary 37 % per row where it covers 87 %.
    float fw = max(fwidth(dist), 1e-6);
    float alpha = (antialias > 0.5) ? saturate((hw - dist) / fw + 0.5) : ((dist <= hw) ? 1.0 : 0.0);
    if (alpha <= 0.0)
        discard;
    return float4(i.color.rgb, i.color.a * alpha);
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
