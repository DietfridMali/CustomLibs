#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"
#include "linerenderer.h"

// =================================================================================================
// Line ribbon shader (DX). The general purpose line drawer of the library (LineRenderer,
// include/linerenderer.h): one line per instance, pulled from the structured buffer (t0 space1) and expanded
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
	{ "Vertex", 0, ShaderDataAttributes::Float3 },
	{ "TexCoord", 0, ShaderDataAttributes::Float2 },
};

static const ShaderDataAttributes LineFacesQuadAttrs[] = {
	{ "Vertex", 0, ShaderDataAttributes::Float3 },
	{ "TexCoord", 0, ShaderDataAttributes::Float2 },
	{ "Uint", 0, ShaderDataAttributes::Uint1, true },
};

static const String LineDrawVS = String(R"(
struct LineInstance {
    float3 p0;    float width;   // width in target pixels
    float3 p1;    float style;   // 0 solid, 1 dashed, 2 dotted, 3 dash-dot
    float4 color;
    float  phase; float3 pad;    // pattern offset at p0 (view units) - strip continuity
};

StructuredBuffer<LineInstance> lines : register(t0, space1);

#ifdef LINE_FACES
struct LineFaces {
    float3 n0;    float contourWidth;
    float3 n1;    float contourAlpha;
};

StructuredBuffer<LineFaces> lineFaces : register(t1, space1);
#endif

cbuffer FrameConstants : register(b0) {
    column_major float4x4 mModelView;
    column_major float4x4 mProjection;
    column_major float4x4 mViewport;
    FRAME_EYE_MATRICES
};

cbuffer ShaderConstants : register(b1) {   // same block as the PS
    float2 texelSize;      // one over the TARGET BUFFER size - mClip below is in the buffer's NDC
    float  perspective;    // 1: pixels per unit shrink with depth; 0: orthographic projection
    float  dashScale;      // stretches the dash / dot pattern (PS)
    float  antialias;      // 1: analytic edge antialiasing, 0: hard edge (PS)
    int    firstLine;      // index of this draw's first record in the buffer
    float  viewerPull;
    float  worldWidth;
    float  darken;
#ifdef LINE_FACES
    float  widthFadeDistance;
    float  alphaFadeScale;
    float  depthPull;
    float  widthRefDistance;
    float  minWidthScale;
    float  haloReach;
    float  haloStrength;
    float  occluderTolerance;
#endif
#ifdef LINE_HIDDEN
    float  occluderMaskLimit;
#endif
};

static const float minWorldHalfWidth = 0.375;

struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float2 tc  : TEXCOORD;
#ifdef LINE_FACES
    [[vk::location(13)]] uint slot : UINT0;
#endif
    uint   iid : SV_InstanceID;
};

struct PSInput {
    float4 pos     : SV_Position;
    float4 cap     : TEXCOORD0;   // (along, across, segLen, halfWidth) - capsule local, PIXELS
    float4 color   : TEXCOORD1;
    float4 pattern : TEXCOORD2;   // (style, phase, caps, 0)
    float2 ends    : TEXCOORD3;   // flat end: overhang, miter end: cut slope - PIXELS
#ifdef LINE_FACES
    float4 edge       : TEXCOORD4;
    float4 edgeNear   : TEXCOORD5;
    float2 edgeAcross : TEXCOORD6;
#endif
};

float3 PullToViewer(float3 vp) {
    if (viewerPull <= 0.0)
        return vp;
    float d = length(vp);
    if (d <= 1e-4)
        return vp;
    return vp - vp * (viewerPull / (d * pow(d, 0.25)));
}

PSInput VSMain(VSInput v VIEW_ID_PARAM) {
    SET_VIEW_ID
#ifdef LINE_FACES
    uint lineIndex = v.slot + uint(firstLine);
#else
    uint lineIndex = v.iid + uint(firstLine);
#endif
    LineInstance l = lines[lineIndex];
    float lineWidth = l.width;
    float4 lineColor = l.color;
#ifdef LINE_FACES
    LineFaces f = lineFaces[lineIndex];
    float3 vMid = mul(mModelView, float4(0.5 * (l.p0 + l.p1), 1.0)).xyz;
    float front0 = (dot(mul(mModelView, float4(f.n0, 0.0)).xyz, vMid) < 0.0) ? 1.0 : 0.0;
    float front1 = (dot(mul(mModelView, float4(f.n1, 0.0)).xyz, vMid) < 0.0) ? 1.0 : 0.0;
    if (front0 + front1 < 1.5) {
        lineWidth = f.contourWidth;
        lineColor.a = f.contourAlpha;
    }
    float drawn = ((front0 + front1 > 0.5) ? 1.0 : 0.0) * ((lineWidth > 0.0) ? 1.0 : 0.0);
    float viewDist = max(length(vMid), 1e-4);
    if (alphaFadeScale > 0.0)
        lineColor.a *= saturate(alphaFadeScale / viewDist);
#endif

    float3 vp0 = PullToViewer(mul(mModelView, float4(l.p0, 1.0)).xyz);
    float3 vp1 = PullToViewer(mul(mModelView, float4(l.p1, 1.0)).xyz);

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
    perp = (perpLen > 1e-4) ? perp / perpLen : normalize(cross(toEye, (abs(toEye.x) < 0.9) ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0)));
    float3 side = cross(toEye, perp);

    // Pixels one view unit covers on screen, ACROSS the line and ALONG it: the projection's derivative
    // along perp and along axis at the line's middle, exact at that depth for a perspective projection
    // and for any orthographic one. The two differ - a 2D ortho over a wide viewport has more pixels
    // per unit in x than in y - so the capsule is handed to the PS in PIXELS. Measured in view units
    // it was an ellipse: the round caps of a horizontal line stuck out 2.7 times further than those of
    // a vertical one on a menu frame, and a width taken from the y scale alone came out 1.78 times too
    // wide on a vertical line.
    float4x4 mClip = mul(mViewport, VIEW_PROJECTION);
    float4 cMid = mul(mClip, float4(0.5 * (vp0 + vp1), 1.0));
    float4 cPerp = mul(mClip, float4(perp, 0.0));
    float4 cAxis = mul(mClip, float4(axis, 0.0));
    float4 cSide = mul(mClip, float4(side, 0.0));
    float wMid = (abs(cMid.w) > 1e-6) ? cMid.w : 1e-6;
    float2 pxScale = 0.5 / texelSize;
    float2 dPerpNdc = (cPerp.xy - (cMid.xy / wMid) * cPerp.w) / wMid;   // d(ndc) / d(perp)
    float2 dAxisNdc = (cAxis.xy - (cMid.xy / wMid) * cAxis.w) / wMid;   // d(ndc) / d(axis)
    float2 dSideNdc = (cSide.xy - (cMid.xy / wMid) * cSide.w) / wMid;   // d(ndc) / d(side)
    float pxAcross = max(length(dPerpNdc * pxScale), 1e-6);
    float pxAlong = length(dAxisNdc * pxScale);
    float pxSide = max(length(dSideNdc * pxScale), 1e-6);
    float halfWidth = max(0.5 * lineWidth, 1e-3);   // pixels
    if (worldWidth > 0.0)
        halfWidth = max(min(halfWidth, 0.5 * worldWidth * pxAcross), minWorldHalfWidth);
#ifdef LINE_FACES
    if (widthFadeDistance > 0.0)
        halfWidth = max(halfWidth * saturate(1.0 - viewDist / widthFadeDistance), 0.5);
    if (widthRefDistance > 0.0)
        halfWidth = max(halfWidth * clamp(widthRefDistance / viewDist, minWidthScale, 1.0), 0.5);
#endif
    // one pixel of margin around the capsule, so the antialiased edge has the pixels it fades over
    float halfDraw = halfWidth + 1.0;
    float haloWidth = 0.0;
#ifdef LINE_FACES
    haloWidth = haloReach * halfWidth;
    halfDraw += haloWidth;
#endif

    // capsule bounding box: x along the line (round cap before p0 .. round cap after p1), y across -
    // spanned in view units, so the pixel margins go back through the scale of their direction
    int caps = int(l.pad.x + 0.5);
    float slopeScale = pxAlong / pxAcross;
    float startValue = ((caps & 4) != 0) ? clamp(l.pad.y * slopeScale, -8.0, 8.0) : l.pad.y;
    float endValue = ((caps & 8) != 0) ? clamp(l.pad.z * slopeScale, -8.0, 8.0) : l.pad.z;
    float startReach = ((caps & 4) != 0) ? max(halfDraw, halfWidth * abs(startValue) + 1.0) : ((caps & 1) != 0) ? max(halfDraw, startValue + 1.0) : halfDraw;
    float endReach = ((caps & 8) != 0) ? max(halfDraw, halfWidth * abs(endValue) + 1.0) : ((caps & 2) != 0) ? max(halfDraw, endValue + 1.0) : halfDraw;
    int column = int(v.tc.x + 0.5);
    float segPx = segLen * pxAlong;
    float along = (column == 0) ? -startReach : (column == 1) ? 0.0 : (column == 2) ? segPx : segPx + endReach;
    float across = v.pos.y * 2.0 * halfDraw / pxAcross;
    float3 node = (column == 0) ? vp0 - side * (startReach / pxSide) : (column == 1) ? vp0 : (column == 2) ? vp1 : vp1 + side * (endReach / pxSide);
    node += perp * across;
#ifdef LINE_FACES
    float3 edgePoint = (column <= 1) ? vp0 : vp1;
    float3 edgeNear = edgePoint + float3(0.0, 0.0, occluderTolerance);
    if (perspective > 0.5) {
        float pullScale = 1.0 - min(depthPull * (halfWidth + 1.0) / (pxAcross * max(length(0.5 * (vp0 + vp1)), 1e-4)), 0.5);
        node *= pullScale;
        edgePoint *= pullScale;
        edgeNear.z = min(edgeNear.z, -1e-4);
    }
#endif

    PSInput o;
    o.pos = mul(mViewport, mul(VIEW_PROJECTION, float4(node, 1.0)));
#ifdef LINE_FACES
    if (drawn < 0.5)
        o.pos = float4(0.0, 0.0, -2.0, 1.0);
    o.edge = mul(mViewport, mul(VIEW_PROJECTION, float4(edgePoint, 1.0)));
    o.edgeNear = mul(mViewport, mul(VIEW_PROJECTION, float4(edgeNear, 1.0)));
    o.edgeAcross = float2(dPerpNdc.x * pxScale.x, -dPerpNdc.y * pxScale.y) / pxAcross;
#endif
    o.cap = float4(along, across * pxAcross, segPx, halfWidth);
    o.color = lineColor;
    o.pattern = float4(l.style, l.phase * pxAlong, l.pad.x, haloWidth);
    o.ends = float2(startValue, endValue);
    return o;
}
)");

static const String LineDrawFS = String(R"(
struct PSInput {
    float4 pos     : SV_Position;
    float4 cap     : TEXCOORD0;
    float4 color   : TEXCOORD1;
    float4 pattern : TEXCOORD2;
    float2 ends    : TEXCOORD3;
#ifdef LINE_FACES
    float4 edge       : TEXCOORD4;
    float4 edgeNear   : TEXCOORD5;
    float2 edgeAcross : TEXCOORD6;
#endif
};

cbuffer ShaderConstants : register(b1) {
    float2 texelSize;
    float  perspective;
    float  dashScale;      // stretches the dash / dot pattern (1 = the lengths above)
    float  antialias;      // 1: analytic edge antialiasing, 0: hard edge
    int    firstLine;
    float  viewerPull;
    float  worldWidth;
    float  darken;
#ifdef LINE_FACES
    float  widthFadeDistance;
    float  alphaFadeScale;
    float  depthPull;
    float  widthRefDistance;
    float  minWidthScale;
    float  haloReach;
    float  haloStrength;
    float  occluderTolerance;
#endif
#ifdef LINE_HIDDEN
    float  occluderMaskLimit;
#endif
};

// distance of t to the interval [s, e] along the line, 0 inside
float IntervalDist(float t, float s, float e) {
    return max(max(s - t, t - e), 0.0);
}

#ifdef LINE_HIDDEN
ScreenTexture occluderDepth : register(t2);
ScreenTexture occluderMask  : register(t3);

float4 PSMain(PSInput i VIEW_ID_PARAM) : SV_Target {
    SET_VIEW_ID
    int2 pixel = int2(i.pos.xy);
    if (i.pos.z <= occluderDepth.Load(SCREEN_TEXEL(pixel)).r)
        discard;
    if (occluderMask.Load(SCREEN_TEXEL(pixel)).a > occluderMaskLimit)
        discard;
#elif defined(LINE_FACES)
ScreenTexture sceneDepth : register(t2);

float SceneDepthAt(float2 pixel, int2 limit) {
    return sceneDepth.Load(SCREEN_TEXEL(clamp(int2(pixel), int2(0, 0), limit))).r;
}

float4 PSMain(PSInput i VIEW_ID_PARAM) : SV_Target {
    SET_VIEW_ID
    float ownDepth = sceneDepth.Load(SCREEN_TEXEL(int2(i.pos.xy))).r;
    if (i.pos.z > ownDepth) {
        if (i.edgeNear.z / i.edgeNear.w > ownDepth)
            discard;
        float2 edgeNdc = i.edge.xy / i.edge.w;
        float2 targetSize = 1.0 / texelSize;
        int2 limit = int2(targetSize + 0.5) - 1;
        float2 edgePixel = float2(edgeNdc.x * 0.5 + 0.5, 0.5 - edgeNdc.y * 0.5) * targetSize;
        float farthest = max(SceneDepthAt(edgePixel, limit), max(SceneDepthAt(edgePixel + i.edgeAcross, limit), SceneDepthAt(edgePixel - i.edgeAcross, limit)));
        if (i.edge.z / i.edge.w > farthest)
            discard;
    }
#else
float4 PSMain(PSInput i) : SV_Target {
#endif
    float along  = i.cap.x;
    float across = i.cap.y;
    float segLen = i.cap.z;
    float hw     = i.cap.w;

    int caps = int(i.pattern.z + 0.5);
    bool flatStart = (caps & 1) != 0;
    bool flatEnd = (caps & 2) != 0;
    bool miterStart = (caps & 4) != 0;
    bool miterEnd = (caps & 8) != 0;

    // the line's own ends: outside [0, segLen] the distance grows and the caps come out round
    float dAlong = IntervalDist(along, (flatStart || miterStart) ? -1e20 : 0.0, (flatEnd || miterEnd) ? 1e20 : segLen);

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
#ifdef LINE_FACES
    float haloWidth = i.pattern.w;
    if (haloWidth > 0.0) {
        float fade = 1.0 - saturate((dist - hw) / haloWidth);
        alpha = max(alpha, haloStrength * fade * fade);
    }
#endif
    float fa = max(fwidth(along), 1e-6);
    if (flatStart || flatEnd) {
        float dEnd = max(flatStart ? -i.ends.x - along : -1e20, flatEnd ? along - segLen - i.ends.y : -1e20);
        alpha *= (antialias > 0.5) ? saturate(0.5 - dEnd / fa) : ((dEnd <= 0.0) ? 1.0 : 0.0);
    }
    if (miterStart && (along < -i.ends.x * across - 0.25))
        discard;
    if (miterEnd && (along > segLen + i.ends.y * across + 0.25))
        discard;
    if (alpha <= 0.0)
        discard;
    float a = i.color.a * alpha;
    if (darken > 0.5)
        return float4(lerp(float3(1.0, 1.0, 1.0), i.color.rgb, a), 1.0);
    return float4(i.color.rgb, a);
}
)");

const ShaderSource& LineDrawShader()
{
	static const ShaderSource source(
		"lineDraw",
		LineDrawVS,
		LineDrawFS,
		ShaderDataLayout(LineQuadAttrs, 2));
	return source;
}


const ShaderSource& LineDrawHiddenShader()
{
	static const ShaderSource source(
		"lineDrawHidden",
		LineDrawVS,
		LineDrawFS,
		ShaderDataLayout(LineQuadAttrs, 2),
		{ ShaderMacro{ String("LINE_HIDDEN"), String("1") } });
	return source;
}


const ShaderSource& LineDrawFacesShader()
{
	static const ShaderSource source(
		"lineDrawFaces",
		LineDrawVS,
		LineDrawFS,
		ShaderDataLayout(LineFacesQuadAttrs, 3),
		{ ShaderMacro{ String("LINE_FACES"), String("1") } });
	return source;
}


static const String LineCullCS = String(R"(
struct LineInstance {
    float3 p0;    float width;
    float3 p1;    float style;
    float4 color;
    float  phase; float3 pad;
};

struct LineFaces {
    float3 n0;    float contourWidth;
    float3 n1;    float contourAlpha;
};

struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint firstInstance;
};

StructuredBuffer<LineInstance>  lines        : register(t0, space1);
StructuredBuffer<LineFaces>     lineFaces    : register(t1, space1);
RWStructuredBuffer<uint>        visibleLines : register(u0);
RWStructuredBuffer<DrawCommand> drawCommands : register(u1);

cbuffer FrameConstants : register(b0) {
    column_major float4x4 mModelView;
    column_major float4x4 mProjection;
    column_major float4x4 mViewport;
    FRAME_EYE_MATRICES
};

cbuffer ShaderConstants : register(b1) {
    float2 texelSize;
    uint   lineCount;
    uint   firstCommand;
    float  minLength;
    float  haloReach;
    float  depthPull;
    uint   depthLevelCount;
    int    screenWidth;
    int    screenHeight;
};

float4x4 EyeProjection(uint eye) {
#if VIEW_COUNT > 1
    return mEyeProjection[eye];
#else
    return mProjection;
#endif
}

#ifdef LINE_DEPTH
ScreenTexture<float> depthLevel0  : register(t0);
ScreenTexture<float> depthLevel1  : register(t1);
ScreenTexture<float> depthLevel2  : register(t2);
ScreenTexture<float> depthLevel3  : register(t3);
ScreenTexture<float> depthLevel4  : register(t4);
ScreenTexture<float> depthLevel5  : register(t5);
ScreenTexture<float> depthLevel6  : register(t6);
ScreenTexture<float> depthLevel7  : register(t7);
ScreenTexture<float> depthLevel8  : register(t8);
ScreenTexture<float> depthLevel9  : register(t9);
ScreenTexture<float> depthLevel10 : register(t10);
ScreenTexture<float> depthLevel11 : register(t11);

float LoadDepth(uint level, int2 texel, uint eye) {
#if VIEW_COUNT > 1
    int4 at = int4(texel, int(eye), 0);
#else
    int3 at = int3(texel, 0);
#endif
    if (level == 0u)
        return depthLevel0.Load(at);
    if (level == 1u)
        return depthLevel1.Load(at);
    if (level == 2u)
        return depthLevel2.Load(at);
    if (level == 3u)
        return depthLevel3.Load(at);
    if (level == 4u)
        return depthLevel4.Load(at);
    if (level == 5u)
        return depthLevel5.Load(at);
    if (level == 6u)
        return depthLevel6.Load(at);
    if (level == 7u)
        return depthLevel7.Load(at);
    if (level == 8u)
        return depthLevel8.Load(at);
    if (level == 9u)
        return depthLevel9.Load(at);
    if (level == 10u)
        return depthLevel10.Load(at);
    return depthLevel11.Load(at);
}

float PulledDepth(float3 p, float pull, float4x4 mClip) {
    float4 v = mul(mModelView, float4(p, 1.0));
    v.xyz *= 1.0 - pull;
    float4 c = mul(mClip, v);
    return c.z / c.w;
}

bool IsHidden(float3 p0, float3 p1, float4 c0, float4 c1, float lineWidth, float reach, float4x4 mClip, float4x4 mEye, uint eye) {
    float2 screen = float2(float(screenWidth), float(screenHeight));
    float2 s0 = float2(c0.x / c0.w * 0.5 + 0.5, 0.5 - c0.y / c0.w * 0.5) * screen;
    float2 s1 = float2(c1.x / c1.w * 0.5 + 0.5, 0.5 - c1.y / c1.w * 0.5) * screen;
    int2 low = int2(clamp(min(s0, s1) - reach, float2(0.0, 0.0), screen - 1.0));
    int2 high = int2(clamp(max(s0, s1) + reach, float2(0.0, 0.0), screen - 1.0));
    uint level = depthLevelCount;
    for (uint k = 0u; k < depthLevelCount; ++k) {
        int2 t0 = low >> int(k + 1u);
        int2 t1 = high >> int(k + 1u);
        if (max(t1.x - t0.x, t1.y - t0.y) <= 1) {
            level = k;
            break;
        }
    }
    if (level >= depthLevelCount)
        return false;
    int shift = int(level + 1u);
    int2 size = (int2(screenWidth, screenHeight) + (1 << shift) - 1) >> shift;
    int2 t0 = min(low >> shift, size - 1);
    int2 t1 = min(high >> shift, size - 1);
    float farthest = max(max(LoadDepth(level, t0, eye), LoadDepth(level, int2(t1.x, t0.y), eye)), max(LoadDepth(level, int2(t0.x, t1.y), eye), LoadDepth(level, t1, eye)));
    float pull = min(depthPull * (0.5 * lineWidth + 1.0) / (0.5 * abs(mEye[1][1]) * float(screenHeight)), 0.5);
    float nearest = min(PulledDepth(p0, pull, mClip), PulledDepth(p1, pull, mClip));
    return nearest > farthest;
}
#endif

bool IsVisible(LineInstance l, float lineWidth, float reach, float2 scale, uint eye) {
    float4x4 mEye = EyeProjection(eye);
    float4x4 mClip = mul(mViewport, mEye);
    float4 c0 = mul(mClip, mul(mModelView, float4(l.p0, 1.0)));
    float4 c1 = mul(mClip, mul(mModelView, float4(l.p1, 1.0)));
    if (min(c0.x - c0.w * scale.x, c1.x - c1.w * scale.x) > 0.0)
        return false;
    if (min(-c0.x - c0.w * scale.x, -c1.x - c1.w * scale.x) > 0.0)
        return false;
    if (min(c0.y - c0.w * scale.y, c1.y - c1.w * scale.y) > 0.0)
        return false;
    if (min(-c0.y - c0.w * scale.y, -c1.y - c1.w * scale.y) > 0.0)
        return false;
    if (min(-c0.z, -c1.z) > 0.0)
        return false;
    if (min(c0.z - c0.w, c1.z - c1.w) > 0.0)
        return false;
    if (min(c0.w, c1.w) > 0.0) {
        float2 span = (c1.xy / c1.w - c0.xy / c0.w) / (2.0 * texelSize);
        if (length(span) < minLength)
            return false;
#ifdef LINE_DEPTH
        if (IsHidden(l.p0, l.p1, c0, c1, lineWidth, reach, mClip, mEye, eye))
            return false;
#endif
    }
    return true;
}

[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    uint index = id.x;
    if (index >= lineCount)
        return;
    LineInstance l = lines[index];
    LineFaces f = lineFaces[index];
    float3 vMid = mul(mModelView, float4(0.5 * (l.p0 + l.p1), 1.0)).xyz;
    float front0 = (dot(mul(mModelView, float4(f.n0, 0.0)).xyz, vMid) < 0.0) ? 1.0 : 0.0;
    float front1 = (dot(mul(mModelView, float4(f.n1, 0.0)).xyz, vMid) < 0.0) ? 1.0 : 0.0;
    if (front0 + front1 < 0.5)
        return;
    float lineWidth = (front0 + front1 > 1.5) ? l.width : f.contourWidth;
    if (lineWidth <= 0.0)
        return;
    float reach = 0.5 * lineWidth * (1.0 + haloReach) + 1.0;
    float2 scale = 1.0 + 2.0 * reach * texelSize;
    for (uint eye = 0u; eye < uint(VIEW_COUNT); ++eye) {
        if (IsVisible(l, lineWidth, reach, scale, eye)) {
            uint slot;
            InterlockedAdd(drawCommands[firstCommand].instanceCount, 1u, slot);
            visibleLines[slot] = index;
            return;
        }
    }
}
)");


static AutoArray<ComputeBindingDesc> MakeLineCullBindings(int depthLevels)
{
	AutoArray<ComputeBindingDesc> b;
	b.SetAutoFit(true);
	using Kind = ComputeBindingDesc::Kind;
	*b.Append() = { .binding = 0, .kind = Kind::UniformBuffer, .count = 1 };
	*b.Append() = { .binding = 1, .kind = Kind::UniformBuffer, .count = 1 };
	for (int i = 0; i < depthLevels; ++i)
		*b.Append() = { .binding = uint32_t(4 + i), .kind = Kind::SampledImage, .count = 1 };
	*b.Append() = { .binding = 36, .kind = Kind::StorageBuffer, .count = 1 };
	*b.Append() = { .binding = 37, .kind = Kind::StorageBuffer, .count = 1 };
	*b.Append() = { .binding = 42, .kind = Kind::ReadOnlyBuffer, .count = 1 };
	*b.Append() = { .binding = 43, .kind = Kind::ReadOnlyBuffer, .count = 1 };
	return b;
}


const ShaderSource& LineCullShader()
{
	static AutoArray<ComputeBindingDesc>	bindings = MakeLineCullBindings(0);
	static const ShaderSource				source(
		   "lineCull",
		   { .cs = LineCullCS,
					 .computeBindings = bindings });
	return source;
}


const ShaderSource& LineCullDepthShader()
{
	static AutoArray<ComputeBindingDesc>	bindings = MakeLineCullBindings(LineRenderer::cMaxDepthLevels);
	static const ShaderSource				source(
		   "lineCullDepth",
		   { .cs = String("#define LINE_DEPTH 1\n") + LineCullCS,
					 .computeBindings = bindings });
	return source;
}

// =================================================================================================
