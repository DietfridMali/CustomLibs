#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// This shader lives in rendertools because both applications draw their lightning with it - the
// generating side (LightningBolt / LightningSystem / LightningEmitter) moved here before it. Anything
// an application wants to look different is a uniform, not a second copy of the source.
//
// Lightning ribbon shader (DX). Each polyline segment is one instance; the VS pulls the segment from
// the structured buffer (u0) and expands the unit quad into a screen-space ribbon. The width offset is
// laid along the miter (bisector) of the two adjacent segments in view space, so neighbouring segments
// that share a node emit the SAME edge and the strip stays gap-/overlap-free at the joints. The FS
// draws a white core with a cool-blue halo, additively into the dedicated glow buffer (HDR -> bloom).

static const ShaderDataAttributes LightningQuadAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};

static const String LightningDrawVS = String(R"(
struct LightningSegment {
    float3 p0;   float w0;
    float3 p1;   float w1;
    float3 prev; float fade;
    float3 next; float coreWidth;
    float3 color; float pad;
};

RWStructuredBuffer<LightningSegment> segments : register(u0);

cbuffer FrameConstants : register(b0) {
    column_major float4x4 mModelView;
    column_major float4x4 mProjection;
    column_major float4x4 mViewport;
};

cbuffer ShaderConstants : register(b1) {   // same layout as the FS block (VS needs minWidthPx/texelSize)
    float3 coreColor;
    float3 haloColor;
    float  intensity;
    float  softDepthScale;
    float  corePass;
    float  minWidthPx;                 // minimum on-screen CORE-band width in target pixels (0 = off)
    float  haloProfile;                // mantle falloff exponent (used by the FS)
    float2 texelSize;                  // one over the VIEWPORT size (BaseRenderer::TexelSize ())
};

struct VSInput {
    float3 pos : POSITION;
    float2 tc  : TEXCOORD;
    uint   iid : SV_InstanceID;
};

struct PSInput {
    float4 pos : SV_Position;
    float4 cap : TEXCOORD0;   // (along, across, segLen, widthComp) -- capsule-local, view units
    float4 wf  : TEXCOORD1;   // (w0, w1, fade, coreWidth)
    float3 col : TEXCOORD2;   // per-bolt halo tint
};

PSInput VSMain(VSInput v) {
    LightningSegment s = segments[v.iid];

    float3 vp0 = mul(mModelView, float4(s.p0, 1.0)).xyz;
    float3 vp1 = mul(mModelView, float4(s.p1, 1.0)).xyz;

    // The ribbon is spanned in 3D VIEW SPACE, not in the segment's xy projection. With the projection,
    // segLen was the PROJECTED length, so a segment running towards the viewer shrank to nothing: its
    // capsule collapsed and the axis fell back to (1, 0) - the bolt broke up into steps and dashes
    // exactly where it pointed at the camera. Across the segment the ribbon faces the viewer (in view
    // space the eye is the origin), which is what a billboard has to do.
    float3 seg = vp1 - vp0;
    float segLen = length(seg);
    float3 axis = (segLen > 1e-6) ? seg / segLen : float3(1.0, 0.0, 0.0);
    float3 toEye = normalize(-0.5 * (vp0 + vp1));
    float3 perp = cross(axis, toEye);
    float perpLen = length(perp);
    // a bolt seen exactly end-on has no unique across direction - any perpendicular will do
    perp = (perpLen > 1e-4) ? perp / perpLen : normalize(cross(axis, float3(0.0, 0.0, 1.0)));
    float wMax = max(s.w0, s.w1);

    // Minimum on-screen width: a sub-pixel core band rasterizes as dashes/dots (the fragment centres miss
    // the thin capsule band). If the projected CORE band (coreWidth fraction of the half-width) would drop
    // below minWidthPx target pixels, widen the ribbon onto that minimum and hand the widening ratio to the
    // FS, which dims colour AND coverage by it -> the line turns continuous, not brighter.
    float widthComp = 1.0;
    if (minWidthPx > 0.0) {
        float pxPerUnit = abs(mProjection[1][1]) * (0.5 / texelSize.y) / max(abs((vp0.z + vp1.z) * 0.5), 1e-3);
        float wMin = minWidthPx / (pxPerUnit * max(s.coreWidth, 0.1));
        if (wMax < wMin) {
            float grow = wMin / max(wMax, 1e-5);
            s.w0 *= grow;
            s.w1 *= grow;
            wMax = wMin;
            widthComp = 1.0 / grow;
        }
    }

    // Expand the unit quad to the segment's capsule bounding box: x runs along the axis from a round cap
    // before p0 to a round cap after p1, y spans +/- wMax across. The FS evaluates the capsule distance
    // field, so MAX-blended overlapping capsules merge seamlessly at kinks -- no miter fold, any thickness.
    float along  = (v.pos.x + 0.5) * (segLen + 2.0 * wMax) - wMax;   // -wMax .. segLen + wMax
    float across = v.pos.y * 2.0 * wMax;                             // -wMax .. wMax

    float3 node = vp0 + axis * along + perp * across;

    PSInput o;
    o.pos = mul(mViewport, mul(mProjection, float4(node, 1.0)));
    o.cap = float4(along, across, segLen, widthComp);
    o.wf  = float4(s.w0, s.w1, s.fade, s.coreWidth);
    o.col = s.color;
    return o;
}
)");

static const String LightningDrawFS = String(R"(
struct PSInput {
    float4 pos : SV_Position;
    float4 cap : TEXCOORD0;   // (along, across, segLen, widthComp)
    float4 wf  : TEXCOORD1;   // (w0, w1, fade, coreWidth)
    float3 col : TEXCOORD2;   // per-bolt halo tint
};

Texture2D sceneDepth : register(t2);   // full-res opaque scene depth (read via Load) for manual occlusion

cbuffer ShaderConstants : register(b1) {
    float3 coreColor;
    float3 haloColor;
    float  intensity;
    float  softDepthScale;             // half-res glow buffer: scale the fragment xy up to the full-res depth texel; 0 = skip the manual test (core pass)
    float  corePass;                   // 1 = full-res core pass into the scene buffer: HW depth test, draw only the core band
    float  minWidthPx;                 // minimum on-screen CORE-band width in target pixels (0 = off; used by the VS)
    float  haloProfile;                // mantle falloff exponent: < 1 wide and full, > 1 tight around the core
    float2 texelSize;                  // one over the VIEWPORT size (BaseRenderer::TexelSize (); used by the VS)
};

float4 PSMain(PSInput i) : SV_Target {
    // Manual depth test -- the glow buffer has no depth attachment, so occlude the bolt against the opaque
    // scene depth (walls, and smileys via their depth prepass): i.pos.xy is half-res -> scale to the full-res
    // texel, discard fragments behind the surface (same LessEqual convention as the hardware depth test).
    // The full-res core pass renders into the scene buffer with the hardware depth test -> skipped there.
    if (softDepthScale > 0.0) {
        float sceneNdcZ = sceneDepth.Load(int3(int2(i.pos.xy * softDepthScale), 0)).r;
        if (i.pos.z > sceneNdcZ)
            discard;
    }

    // Capsule distance field: distance from the fragment to the segment LINE (round caps at p0/p1), in view
    // units, normalized by the tapered half-width. d = 0 at the core line, 1 at the capsule edge. Overlapping
    // capsules of adjacent segments MAX-blend into a seamless thick stroke (no miter self-fold).
    float along  = i.cap.x;
    float across = i.cap.y;
    float segLen = i.cap.z;
    float w = lerp(i.wf.x, i.wf.y, saturate(along / max(segLen, 1e-6)));   // interpolated half-width (taper)
    float clampedAlong = clamp(along, 0.0, segLen);
    float dist = length(float2(along - clampedAlong, across));
    float d = dist / max(w, 1e-3);
    if (d >= 1.0)
        discard;                                     // outside the capsule -> transparent

    float coreWidth = i.wf.w;                        // per-bolt white-core fraction (from the segment buffer)
    if (corePass > 0.5 && d >= coreWidth)
        discard;                                     // core pass draws only the core band; the halo comes blurred from the glow pass
    // white hot core: soft quadratic falloff inside the core band
    float core = saturate(1.0 - d / max(coreWidth, 1e-3));
    core *= core;
    // blue halo: soft full-width falloff, suppressed inside the core band so the centre stays white and
    // the blue only reads as a rim outside it (clean white core + bluish edge, soft to the edges).
    float x = saturate(1.0 - d);
    float halo = x * x * (3.0 - 2.0 * x);   // fades out AT the rim (zero value and zero slope there)
    halo = pow(halo, haloProfile);          // < 1 fills the mantle, > 1 pulls it in towards the core
    // AFTERGLOW: a negative fade flags the halo-only afterglow (the core pass dropped this strike). The
    // core-band suppression would leave a dark groove along the axis with nothing overpainting it (shows
    // as periodic holes) -> keep the full halo profile there instead.
    if (i.wf.z >= 0.0)
        halo *= smoothstep(0.0, coreWidth, d);   // suppress blue inside the core band -- linear (not squared): no dark ring between core and halo
    // Premultiplied colour + a clean COMBINED coverage for alpha compositing. rgb = (coreColor*core +
    // haloColor*halo)*intensity is the emitted colour (already premultiplied by its own coverage). alpha
    // combines BOTH core and halo -- crucial so the white core carries alpha too: with alpha = halo alone the
    // core region is 0 and the blur drags the mantle alpha down toward it. The over-combine (a+b-a*b) makes
    // overlapping core+halo pixels read HIGHER, never lower, and stays <= 1. fade is the ttl fade.
    float fade = abs(i.wf.z);   // AFTERGLOW: the sign only flags the afterglow, the magnitude is the ttl fade
    // Kern + Mantel ADDIERT -> rgb (Farbe). Getrenntes echtes Alpha (Deckung) im Alphakanal, NICHT premultipliziert
    // (rgb wird nicht mit alpha multipliziert). fade (ttl) geht in rgb UND alpha: beide Composites blenden One/One(+Max)
    // und ignorieren Alpha dabei -> dunkler wird der Blitz beim Ausklingen nur ueber rgb.
    // widthComp (< 1 where the VS widened a sub-pixel ribbon onto the minimum screen width) dims colour AND
    // coverage by the widening ratio -> constant perceived energy, continuous line.
    float widthComp = i.cap.w;
    float3 col   = (coreColor * core + haloColor * i.col * halo) * (intensity * widthComp * fade);
    float  alpha = (core + halo - core * halo) * fade * widthComp;
    return float4(col, alpha);
}
)");

const ShaderSource& LightningDrawShader() {
    static const ShaderSource source(
        "lightningDraw",
        LightningDrawVS,
        LightningDrawFS,
        ShaderDataLayout(LightningQuadAttrs, 2)
    );
    return source;
}

// =================================================================================================
// Impact flare (DX). A round, view-aligned billboard at each bolt endpoint (strike: end; arc: start +
// end), instanced from the LightningFlare buffer. The FS depth-tests against the scene depth like the
// ribbon FS (the soft smoothstep fade stays disabled, occ = 1); a small nudge toward the viewer lifts
// the flare off the endpoint onto the front face, so it still reads as an impact ON the surface it hits.

static const String LightningFlareVS = String(R"(
struct LightningFlare {
    float3 position; float fade;
    float  width;    float3 color;
};

RWStructuredBuffer<LightningFlare> flares : register(u0);

cbuffer FrameConstants : register(b0) {
    column_major float4x4 mModelView;
    column_major float4x4 mProjection;
    column_major float4x4 mViewport;
};

cbuffer ShaderConstants : register(b1) {
    float3 coreColor;
    float  coreWidth;
    float3 haloColor;
    float  intensity;
    float  flareScale;
    float  viewerOffset;
    float  softDepthScale;
    float  depthFade;
    float  flareAspect;
    float  haloProfile;
};

struct VSInput {
    float3 pos : POSITION;
    float2 tc  : TEXCOORD;
    uint   iid : SV_InstanceID;
};

struct PSInput {
    float4 pos  : SV_Position;
    float2 tc   : TEXCOORD0;
    float  fade : TEXCOORD1;
    float3 col  : TEXCOORD2;
};

PSInput VSMain(VSInput v) {
    LightningFlare f = flares[v.iid];
    float3 vc = mul(mModelView, float4(f.position, 1.0)).xyz;   // view-space centre
    vc += normalize(-vc) * viewerOffset;                        // toward the viewer (origin in view space) -> onto the front face
    vc.xy += v.pos.xy * (f.width * flareScale) / float2(1.0, flareAspect);   // view-aligned billboard; flareAspect > 1 stretches X -> wide/flat ellipse

    PSInput o;
    o.pos = mul(mViewport, mul(mProjection, float4(vc, 1.0)));
    o.tc = v.tc;
    o.fade = f.fade;
    o.col = f.color;
    return o;
}
)");

static const String LightningFlareFS = String(R"(
struct PSInput {
    float4 pos  : SV_Position;
    float2 tc   : TEXCOORD0;
    float  fade : TEXCOORD1;
    float3 col  : TEXCOORD2;
};

Texture2D sceneDepth : register(t2);

cbuffer ShaderConstants : register(b1) {
    float3 coreColor;
    float  coreWidth;
    float3 haloColor;
    float  intensity;
    float  flareScale;
    float  viewerOffset;
    float  softDepthScale;
    float  depthFade;
    float  flareAspect;
    float  haloProfile;
};

float4 PSMain(PSInput i) : SV_Target {
    float2 uv = i.tc * 2.0 - 1.0;                    // [-1,1], centre at 0
    float r = length(uv);
    if (r > 1.0)
        discard;                                     // clip the quad corners to a disc
    // soft depth fade, not a hard cull: full where the surface is at/just behind the flare (impact on
    // the smiley), fading out where geometry is clearly in front (a nearer wall would occlude it).
    float sceneNdcZ = sceneDepth.Load(int3(int2(i.pos.xy * softDepthScale), 0)).r;
    // The glow buffer has no depth attachment -> without this test the flare was drawn through walls
    // (occ was hard-wired to 1, so the sampled depth was never used). Same manual LessEqual test as the
    // ribbon FS. The flare centre is nudged viewerOffset (= smiley radius) toward the viewer in the VS,
    // so the impact flare still passes over the smiley it hits.
    if (i.pos.z > sceneNdcZ)
        discard;
    float occ = 1.0; // - smoothstep(0.0, depthFade, i.pos.z - sceneNdcZ);
    float core = saturate(1.0 - r / max(coreWidth, 1e-3));
    core *= core;
    float x = saturate(1.0 - r);
    float halo = x * x * (3.0 - 2.0 * x);
    halo = pow(halo, haloProfile);
    // Premultiplied colour + combined coverage, same convention as the ribbon FS (alpha over-combines core+halo).
    float f = i.fade * occ;
    // wie der Blitz: f (fade*occ) in rgb UND alpha -- der additive Composite ignoriert Alpha.
    float3 col   = (coreColor * core + haloColor * i.col * halo) * (intensity * f);
    float  alpha = (core + halo - core * halo) * f;
    return float4(col, alpha);
}
)");

const ShaderSource& LightningFlareShader() {
    static const ShaderSource source(
        "lightningFlare",
        LightningFlareVS,
        LightningFlareFS,
        ShaderDataLayout(LightningQuadAttrs, 2)
    );
    return source;
}

// =================================================================================================
