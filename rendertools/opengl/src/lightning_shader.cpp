#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// This shader lives in rendertools because both applications draw their lightning with it - the
// generating side (LightningBolt / LightningSystem / LightningEmitter) moved here before it. Anything
// an application wants to look different is a uniform, not a second copy of the source.
//
// Lightning ribbon shader (OpenGL). Mirrors directx/src/lightning_shader.cpp. Each polyline segment is
// one instance; the VS pulls the segment from the SSBO (binding 0) via gl_InstanceID and expands the
// unit quad into a screen-space ribbon along the miter (bisector) of the two adjacent segments, so
// neighbouring segments share an edge -> gap-/overlap-free joints. The FS draws a white core with a
// cool-blue halo, additively into the dedicated glow buffer (HDR -> bloom). std430 inflates vec3 to a
// 16-byte alignment, so the segment struct is declared as flat floats to match the 64-byte C++ layout.

static const ShaderDataAttributes LightningQuadAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};

static const String LightningDrawVS = String(R"(#version 430 core
struct LightningSegment {
    float p0x;   float p0y;   float p0z;   float w0;
    float p1x;   float p1y;   float p1z;   float w1;
    float prevx; float prevy; float prevz; float fade;
    float nextx; float nexty; float nextz; float coreWidth;
    float colr;  float colg;  float colb;  float pad;
};

layout(std430, binding = 0) buffer Segments { LightningSegment segments[]; };

uniform mat4 mModelView;
uniform mat4 mProjection;
uniform mat4 mViewport;
uniform float minWidthPx;       // minimum on-screen CORE-band width in target pixels (0 = off)
uniform vec2 texelSize;         // one over the VIEWPORT size (BaseRenderer::TexelSize ())

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;

out vec4 vCap;   // (along, across, segLen, widthComp) -- capsule-local, view units
out vec4 vWf;    // (w0, w1, fade, coreWidth)
out vec3 vColor; // per-bolt halo tint (multiplies haloColor)

void main() {
    LightningSegment s = segments[uint(gl_InstanceID)];

    vec3 vp0 = (mModelView * vec4(s.p0x, s.p0y, s.p0z, 1.0)).xyz;
    vec3 vp1 = (mModelView * vec4(s.p1x, s.p1y, s.p1z, 1.0)).xyz;

    // The ribbon is spanned in 3D VIEW SPACE, not in the segment's xy projection. With the projection,
    // segLen was the PROJECTED length, so a segment running towards the viewer shrank to nothing: its
    // capsule collapsed and the axis fell back to (1, 0) - the bolt broke up into steps and dashes
    // exactly where it pointed at the camera. Across the segment the ribbon faces the viewer (in view
    // space the eye is the origin), which is what a billboard has to do.
    vec3 seg = vp1 - vp0;
    float segLen = length(seg);
    vec3 axis = (segLen > 1e-6) ? seg / segLen : vec3(1.0, 0.0, 0.0);
    vec3 toEye = normalize(-0.5 * (vp0 + vp1));
    vec3 perp = cross(axis, toEye);
    float perpLen = length(perp);
    // a bolt seen exactly end-on has no unique across direction - any perpendicular will do
    perp = (perpLen > 1e-4) ? perp / perpLen : normalize(cross(axis, vec3(0.0, 0.0, 1.0)));
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

    // Capsule bounding box: x along the axis (round cap before p0 .. round cap after p1), y +/- wMax across.
    // The FS does the capsule distance field, so MAX-blended overlapping capsules merge at kinks (no fold).
    float along  = (position.x + 0.5) * (segLen + 2.0 * wMax) - wMax;
    float across = position.y * 2.0 * wMax;

    vec3 node = vp0 + axis * along + perp * across;

    gl_Position = mViewport * (mProjection * vec4(node, 1.0));
    vCap = vec4(along, across, segLen, widthComp);
    vWf  = vec4(s.w0, s.w1, s.fade, s.coreWidth);
    vColor = vec3(s.colr, s.colg, s.colb);
}
)");

static const String LightningDrawFS = String(R"(#version 430 core
in  vec4 vCap;
in  vec4 vWf;
in  vec3 vColor;
out vec4 fragColor;

uniform sampler2D sceneDepth;      // full-res opaque scene depth for manual occlusion
uniform vec3  coreColor;
uniform vec3  haloColor;
uniform float intensity;
uniform float softDepthScale;      // half-res glow buffer: scale the fragment xy up to the full-res depth texel; 0 = skip the manual test (core pass)
uniform float haloProfile;         // mantle falloff exponent: < 1 wide and full, > 1 tight around the core
uniform float corePass;            // 1 = full-res core pass into the scene buffer: HW depth test, draw only the core band

void main() {
    // Manual depth test -- the glow buffer has no depth attachment, so occlude against the opaque scene depth.
    // The full-res core pass renders into the scene buffer with the hardware depth test -> skipped there.
    if (softDepthScale > 0.0) {
        float sceneNdcZ = texelFetch(sceneDepth, ivec2(gl_FragCoord.xy * softDepthScale), 0).r;
        if (gl_FragCoord.z > sceneNdcZ)
            discard;
    }

    // Capsule distance field: distance to the segment line (round caps at p0/p1), normalized by the tapered
    // half-width. d = 0 at the core line, 1 at the capsule edge. Overlapping capsules MAX-blend seamlessly.
    float along  = vCap.x;
    float across = vCap.y;
    float segLen = vCap.z;
    float w = mix(vWf.x, vWf.y, clamp(along / max(segLen, 1e-6), 0.0, 1.0));   // interpolated half-width (taper)
    float clampedAlong = clamp(along, 0.0, segLen);
    float dist = length(vec2(along - clampedAlong, across));
    float d = dist / max(w, 1e-3);
    if (d >= 1.0)
        discard;                                        // outside the capsule -> transparent

    float coreWidth = vWf.w;                            // per-bolt white-core fraction (from the segment buffer)
    if (corePass > 0.5 && d >= coreWidth)
        discard;                                        // core pass draws only the core band; the halo comes blurred from the glow pass
    float core = clamp(1.0 - d / max(coreWidth, 1e-3), 0.0, 1.0);
    core *= core;
    float x = clamp(1.0 - d, 0.0, 1.0);
    float halo = x * x * (3.0 - 2.0 * x);   // fades out AT the rim (zero value and zero slope there)
    halo = pow(halo, haloProfile);          // < 1 fills the mantle, > 1 pulls it in towards the core
    // AFTERGLOW: a negative fade flags the halo-only afterglow (the core pass dropped this strike). The
    // core-band suppression would leave a dark groove along the axis with nothing overpainting it (shows
    // as periodic holes) -> keep the full halo profile there instead.
    if (vWf.z >= 0.0)
        halo *= smoothstep(0.0, coreWidth, d);
    // Premultiplied colour + clean COMBINED coverage for alpha compositing (see DX shader). rgb = emitted
    // colour; alpha = core+halo over-combined so the white core carries alpha (the blur can't sink the mantle)
    // and overlapping core+halo reads higher, never lower. fade is the ttl fade.
    float fade = abs(vWf.z);   // AFTERGLOW: the sign only flags the afterglow, the magnitude is the ttl fade
    // Kern + Mantel ADDIERT -> rgb (Farbe). Getrenntes echtes Alpha (Deckung), NICHT premultipliziert.
    // fade (ttl) geht in rgb UND alpha: beide Composites blenden One/One(+Max) und ignorieren Alpha dabei
    // -> dunkler wird der Blitz beim Ausklingen nur ueber rgb.
    // widthComp (< 1 where the VS widened a sub-pixel ribbon onto the minimum screen width) dims colour AND
    // coverage by the widening ratio -> constant perceived energy, continuous line.
    float widthComp = vCap.w;
    // vColor is the bolt's own tint: white leaves the shader's haloColor as it is, anything else colours
    // this bolt (d2x-xl has a colour per effect - blue, red, yellow, violet).
    vec3  col   = (coreColor * core + haloColor * vColor * halo) * (intensity * widthComp * fade);
    float alpha = (core + halo - core * halo) * fade * widthComp;
    fragColor = vec4(col, alpha);
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
// Impact flare (OpenGL). Mirrors the DX flare: a round, view-aligned billboard at each bolt endpoint,
// instanced from the LightningFlare SSBO; the FS depth-tests against the scene depth like the ribbon FS
// (the soft smoothstep fade stays disabled, occ = 1). std430 inflates vec3, so the flare struct is declared as flat floats to match
// the 32-byte C++ layout (float3 position; float fade; float width; float3 pad).

static const String LightningFlareVS = String(R"(#version 430 core
struct LightningFlare {
    float posx; float posy; float posz; float fade;
    float width; float colr; float colg; float colb;
};

layout(std430, binding = 0) buffer Flares { LightningFlare flares[]; };

uniform mat4  mModelView;
uniform mat4  mProjection;
uniform mat4  mViewport;
uniform float flareScale;
uniform float viewerOffset;
uniform float flareAspect;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoord;

out vec2  vTc;
out float vFade;
out vec3  vColor;

void main() {
    LightningFlare f = flares[uint(gl_InstanceID)];
    vec3 vc = (mModelView * vec4(f.posx, f.posy, f.posz, 1.0)).xyz;   // view-space centre
    vc += normalize(-vc) * viewerOffset;                             // toward the viewer -> onto the front face
    vc.xy += position.xy * (f.width * flareScale) / vec2(1.0, flareAspect);   // view-aligned billboard; flareAspect > 1 -> wide/flat

    gl_Position = mViewport * (mProjection * vec4(vc, 1.0));
    vTc = texCoord;
    vFade = f.fade;
    vColor = vec3(f.colr, f.colg, f.colb);
}
)");

static const String LightningFlareFS = String(R"(#version 430 core
in  vec2  vTc;
in  float vFade;
in  vec3  vColor;
out vec4  fragColor;

uniform sampler2D sceneDepth;
uniform vec3  coreColor;
uniform float coreWidth;
uniform vec3  haloColor;
uniform float intensity;
uniform float softDepthScale;
uniform float haloProfile;
uniform float depthFade;

void main() {
    vec2 uv = vTc * 2.0 - 1.0;                    // [-1,1], centre at 0
    float r = length(uv);
    if (r > 1.0)
        discard;                                  // clip the quad corners to a disc
    float sceneNdcZ = texelFetch(sceneDepth, ivec2(gl_FragCoord.xy * softDepthScale), 0).r;
    // The glow buffer has no depth attachment -> without this test the flare was drawn through walls
    // (occ was hard-wired to 1, so the sampled depth was never used). Same manual LessEqual test as the
    // ribbon FS. The flare centre is nudged viewerOffset (= smiley radius) toward the viewer in the VS,
    // so the impact flare still passes over the smiley it hits.
    if (gl_FragCoord.z > sceneNdcZ)
        discard;
    float occ = 1.0; // - smoothstep(0.0, depthFade, gl_FragCoord.z - sceneNdcZ);
    float core = clamp(1.0 - r / max(coreWidth, 1e-3), 0.0, 1.0);
    core *= core;
    float x = clamp(1.0 - r, 0.0, 1.0);
    float halo = x * x * (3.0 - 2.0 * x);
    halo = pow(halo, haloProfile);
    // same convention as the ribbon FS: Kern + Mantel addiert -> rgb, echtes Alpha getrennt (kein premult).
    float f = vFade * occ;
    vec3  col   = (coreColor * core + haloColor * vColor * halo) * (intensity * f);
    float alpha = (core + halo - core * halo) * f;
    fragColor = vec4(col, alpha);
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
