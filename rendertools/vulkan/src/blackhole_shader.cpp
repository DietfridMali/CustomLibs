#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================
// This shader lives in rendertools for the same reason the skybox shader next to it does: the draw
// that asks for it is here. Skybox::Render () picks it for sky type 3 (Skybox::LoadBlackholeShader (),
// src/skybox.cpp), which asks its shader handler for the id "blackhole" - so the source has to be
// registered by the library, not by one of its applications.
//
// Only Paintjob Rampage switches this sky on today; it needs the shared 3D cloud shape noise volume
// and a blue noise texture, which Skybox::Setup () takes as arguments and which an application without
// clouds does not have. Without them Skybox::Render () falls back to the ordinary night sky.

static const ShaderDataAttributes VtxAttrs[] = {
    { "Vertex", 0, ShaderDataAttributes::Float3 },
};

// -------------------------------------------------------------------------------------------------
// BlackholeShader: gravitationally lensed accretion disk over a sky cubemap (direction-only).
// The view ray is bent through the hole's field so the disk's far side wraps above and below the
// event horizon; disk structure is sampled from the shared 3D cloud shape-noise volume.
// ShaderConstants: mView, direction, distance, diskNormal, gravity, time, horizon, innerDiskRad, outerDiskRad,
//                  angSpeed, brightness, noiseScale.
const ShaderSource& BlackholeShader() {
    static const ShaderSource source(
        "blackhole",
        R"(
            cbuffer FrameConstants : register(b0) {
                column_major float4x4 mModelView;
                column_major float4x4 mProjection;
            };
            cbuffer ShaderConstants : register(b1) {
                column_major float4x4 mView;
            };
            struct VSInput { [[vk::location(0)]] float3 pos : POSITION; };
            struct PSInput {
                float4 pos           : SV_Position;
                float3 viewDirection : TEXCOORD0;
            };
            PSInput VSMain(VSInput i) {
                PSInput o;
                float4 clipPos = mul(mProjection, mul(mView, float4(i.pos, 1.0)));
                o.pos = float4(clipPos.xy, clipPos.w, clipPos.w);
                o.viewDirection = float3(i.pos.x, i.pos.y, i.pos.z);
                return o;
            }
        )",
        R"(
            cbuffer ShaderConstants : register(b1) {
                column_major float4x4 mView;
                float3 direction;
                float  distance;
                float3 diskNormal;
                float  gravity;
                float  time;
                float  horizon;
                float  innerDiskRad;
                float  outerDiskRad;
                float  angSpeed;
                float  brightness;
                float  noiseScale;
            };
            TextureCube  sky : register(t0);
            Texture3D    noiseTex : register(t1);
            Texture3D    blueNoiseTex : register(t2);
            SamplerState s0 : register(s0);
            SamplerState s1 : register(s1);

            struct PSInput {
                float4 pos : SV_Position;
                float3 viewDirection : TEXCOORD0;
            };

            #define SampleLod(tex, samp, uvw, lod) (tex).SampleLevel(samp, uvw, lod)

            static const int   MAX_STEPS    = 200;
            static const float STEP_MIN     = 0.02;
            static const float STEP_MAX     = 0.60;
            static const float DISK_OPACITY = 1.0;
            static const float PI           = 3.141592653589793;
            static const float TAU           = 6.28318530718;
            static const float TANG_PERIODS  = 2.0;
            static const float RAD_SCALE     = 0.5;
            static const float SPIRAL_TWIST  = 4.0;
            static const float MORPH_SPEED   = 0.02;
            static const float DUST_STRENGTH = 0.7;
            static const float SLAB_THICKNESS   = 0.7;   // full disk thickness [wu]
            static const int   SLAB_SAMPLES     = 3;     // noise samples per march segment inside the slab
            static const float SLAB_DENSITY     = 4.0;   // optical depth of a face-on crossing at full cover
            static const float CLUMP_PERIODS    = 14.0;  // tangential periods of the outer clump layer (integer!)
            static const float CLUMP_WAVELEN    = 3.0;   // world-space clump size [wu]
            static const float CLUMP_BASE       = 0.15;  // density floor between clumps (gap depth)
            static const float CLUMP_START      = 4.5;   // clump layer fades in from this radius ...
            static const float CLUMP_FULL       = 6.5;   // ... and is fully active from here on
            static const float SPIRAL_RMAX      = 5.0;   // spiral shear saturates beyond this radius
            static const float STAR_SECTORS    = 24.0;
            static const int   STAR_LAYERS     = 6;
            static const float STAR_LEN        = 0.0075;
            static const float STAR_WIDTH      = 0.0025;
            static const float STAR_SPEED      = 0.25;
            static const float STAR_BRIGHTNESS = 1.5;
            static const float STAR_FIELD_ANG  = 1.7;  // angular field radius [rad], independent of hole distance
#if 1
            static float Amp(float v) {
                return 0.5f - 0.5f * cos(v * PI);
            }

            static float InvAmp(float v) {
                return 0.5f + 0.5f * cos(v * PI);
            }

            static float Amp2(float v) {
                return Amp(Amp(v));
            }

            float Ridged(float v) {
                return 1.0 - abs(2.0 * v - 1.0);
            }

            float Ridged2(float v) {
                v = Ridged(v);
                return v * v;
            }
#else

#   define Amp2(v)      (v)
#   define Ridged(v)    (v)

#endif

            float3 DiskColor(float t) {
                float3 cool = float3(1.00, 0.42, 0.16);
                float3 mid  = float3(1.00, 0.72, 0.40);
                float3 hot  = float3(1.00, 0.97, 0.92);
                float3 c = lerp(cool, mid, smoothstep(0.0, 0.5, t));
                return lerp(c, hot, smoothstep(0.5, 1.0, t));
            }

            float DiskNoise(float3 p, float lod) {
                float n  = SampleLod(noiseTex, s1, p, lod).r;
                n += 0.50 * SampleLod(noiseTex, s1, p * 2.03 + float3(1.7, 0.0, 0.3), lod).r;
                n += 0.25 * SampleLod(noiseTex, s1, p * 4.07 + float3(0.0, 2.3, 1.1), lod).r;
                return n * 0.5714;
            }

            static float Hash(float v) {
                return frac(sin(v * 12.9898) * 43758.5453);
            }

            // infalling stars: elongated white streaks with a soft gaussian fringe, accelerating
            // toward the hole; laid out in angular sectors around the hole direction, so each pixel
            // only evaluates the streaks of its own sector. Radial coordinate is the true angle to
            // the hole (not gnomonic), so the field radius STAR_FIELD_ANG can exceed 90 degrees and
            // stars always spawn at the far edge of the view, regardless of hole distance.
            float StarStreaks(float3 viewDir) {
                float3 bhDir = normalize(direction);
                float  ang = acos(clamp(dot(viewDir, bhDir), -1.0, 1.0));
                float  angStop = 1.6 * (horizon / distance);
                if ((ang < angStop * 0.5) || (ang > STAR_FIELD_ANG))
                    return 0.0;
                float3 up = (abs(bhDir.y) > 0.99) ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
                float3 e1 = normalize(cross(up, bhDir));
                float3 e2 = cross(bhDir, e1);
                float cellWidth = TAU / STAR_SECTORS;
                float phi = atan2(dot(viewDir, e2), dot(viewDir, e1));
                float cell = floor(phi / cellWidth);
                float a = 0.0;
                [unroll]
                for (int j = 0; j < STAR_LAYERS; ++j) {
                    float fj = float(j);
                    float h0 = Hash(cell * 7.13 + fj * 37.71 + 11.0);
                    float h1 = Hash(cell * 3.77 + fj * 17.29 + 5.0);
                    float h2 = Hash(cell * 9.41 + fj * 23.63 + 2.0);
                    float s = frac(time * STAR_SPEED * (0.6 + 0.8 * h1) + h2);
                    float angStar = lerp(STAR_FIELD_ANG, angStop, s * s);
                    float len = STAR_LEN * (0.6 + 0.8 * h2) * (0.5 + 2.0 * s);
                    float width = STAR_WIDTH * (0.6 + 0.8 * h0);
                    // jitter capped at half the sector so the streak never leaks into a neighbour cell
                    float starPhi = (cell + 0.5 + (h0 - 0.5) * 0.5) * cellWidth;
                    float dPar = (ang - angStar) / len;
                    float dPerp = ((phi - starPhi) * sin(ang)) / width;
                    float fade = smoothstep(0.0, 0.15, s) * (1.0 - smoothstep(0.8, 1.0, s));
                    a += fade * exp(-(dPar * dPar + dPerp * dPerp) * 4.0);
                }
                return saturate(a);
            }

            // point sample inside the disk slab (|height| < SLAB_THICKNESS/2 around the disk plane)
            float4 SampleDisk(float3 rel, float3 tangent, float3 bitangent, float distToCam) {
                float3 inPlane = rel - diskNormal * dot(rel, diskNormal);
                float  radius = length(inPlane);
                if ((radius < innerDiskRad) || (radius > outerDiskRad)) {
                    return float4(0.0, 0.0, 0.0, 0.0);
                }
                float yNorm = dot(rel, diskNormal) / (0.5 * SLAB_THICKNESS);
                if (abs(yNorm) > 1.0) {
                    return float4(0.0, 0.0, 0.0, 0.0);
                }
                float t = (radius - innerDiskRad) / (outerDiskRad - innerDiskRad);
                float tHot = 1.0 - t;

                float theta = atan2(dot(inPlane, bitangent), dot(inPlane, tangent));
                float lod = saturate((distToCam - distance) / distance);

                // shear saturates beyond SPIRAL_RMAX so the outer structure is not dragged into
                // long arcs; the inner spiral is unchanged
                float spiral = SPIRAL_TWIST * sqrt(min(radius, SPIRAL_RMAX) / innerDiskRad);
                float phi = theta + spiral + time * angSpeed;
                float u = phi * (TANG_PERIODS / TAU);
                float v = radius * noiseScale * RAD_SCALE;
                // vertical morph offset: top and bottom of the layer sample different noise slices
                float w = time * MORPH_SPEED + yNorm * 0.15;

                float nBig = SampleLod(noiseTex, s1, float3(u, v, w), lod).r;
                float nFine = SampleLod(noiseTex, s1, float3(u * 3.0 + 0.37, v * 3.0 + 0.61, w * 2.0 + 0.5), lod).r;
                float dens = Amp2(nBig) * (0.15 + 0.85 * Ridged2(nFine));

                float dust = Amp2(SampleLod(noiseTex, s1, float3(u * 2.0 + 0.71, v * 2.0 + 0.13, w * 0.7 + 0.29), lod).r);

                // outer clump layer: near-isotropic world-space blobs (~CLUMP_WAVELEN wu), rigid
                // rotation only (no spiral shear -> round puffs with real gaps between them);
                // radius-gated so the inner disk stays bit-identical
                float clumpGate = smoothstep(CLUMP_START, CLUMP_FULL, radius);
                if (clumpGate > 0.0) {
                    float uClump = (theta + time * angSpeed) * (CLUMP_PERIODS / TAU);
                    float vClump = radius / CLUMP_WAVELEN;
                    float clump = Amp2(SampleLod(noiseTex, s1, float3(uClump, vClump, w + 0.77), lod).r);
                    float shred = lerp(1.0, CLUMP_BASE + (1.0 - CLUMP_BASE) * clump, clumpGate);
                    dens *= shred;
                    dust *= shred;
                }

                float edge = smoothstep(innerDiskRad, innerDiskRad + 0.35, radius) * smoothstep(outerDiskRad, outerDiskRad - 2.0, radius);
                // parabolic vertical profile: full density in the midplane, soft top and bottom faces
                float profile = 1.0 - yNorm * yNorm;
                dens *= edge * profile;
                dust *= edge * profile;

                float bright = lerp(0.30, 1.80, tHot) * brightness;
                float3 col = DiskColor(tHot) * bright * dens;
                col *= lerp(float3(1.0, 1.0, 1.0), float3(0.25, 0.15, 0.10), dust * DUST_STRENGTH);
                float cover = saturate(dens + dust * 0.6);
                return float4(col, cover);
            }

            float4 PSMain(PSInput i) : SV_Target {
                float3 viewDir = normalize(i.viewDirection);
                float3 bhCenter = direction * distance;
                float3 n = normalize(diskNormal);

                if (dot(-bhCenter, n) < 0.0) {
                    float3 flipAxis = n - direction * (dot(n, direction) / dot(direction, direction));
                    float flipLen = length(flipAxis);
                    if (flipLen > 1e-4) {
                        flipAxis = flipAxis / flipLen;
                        viewDir = normalize(viewDir - 2.0 * dot(viewDir, flipAxis) * flipAxis);
                    }
                }

                float3 up = /*(abs(n.y) > 0.99) ? float3(1.0, 0.0, 0.0) :*/ float3(0.0, 1.0, 0.0);
                float3 tangent = normalize(cross(up, n));
                float3 bitangent = cross(n, tangent);

                float3 p = float3(0.0, 0.0, 0.0);
                float3 dir = viewDir;
                float3 col = float3(0.0, 0.0, 0.0);
                float  trans = 1.0;
                bool   captured = false;
                // in-slab path integration state: distance traveled inside the slab so far, and the
                // in-slab distance at which the next sample fires. Sample positions derive only from
                // the ray's slab entry (continuous in the view direction -> no step-grid banding);
                // their spacing follows the local march step, so the sample density matches the
                // adaptive march: fine near the hole, coarse outside.
                float  slabDist = 0.0;
                float  slabNext = 0.0;

                [loop]
                for (int s = 0; s < MAX_STEPS; ++s) {
                    float3 toBH = bhCenter - p;
                    float  dist = length(toBH);
                    float  dt = clamp(0.12 * (dist - horizon) + 0.03, STEP_MIN, STEP_MAX);
                    dir = normalize(dir + (toBH / dist) * (gravity / (dist * dist)) * dt);
                    float3 pPrev = p;
                    p += dir * dt;
                    if (length(bhCenter - p) < horizon) {
                        captured = true;
                        break;
                    }

                    // clip the march segment against the slab and integrate the crossing; the real
                    // path length through the layer replaces the old plane-crossing graze factor.
                    float side0 = dot(pPrev - bhCenter, n);
                    float side1 = dot(p - bhCenter, n);
                    float halfSlab = 0.5 * SLAB_THICKNESS;
                    float dSide = side1 - side0;
                    float kMin = 0.0;
                    float kMax = 0.0;
                    if (abs(dSide) > 1e-6) {
                        float kTop = (halfSlab - side0) / dSide;
                        float kBot = (-halfSlab - side0) / dSide;
                        kMin = max(min(kTop, kBot), 0.0);
                        kMax = min(max(kTop, kBot), 1.0);
                    }
                    else if (abs(side0) < halfSlab) {
                        kMax = 1.0;
                    }
                    if (kMax > kMin) {
                        float L = (kMax - kMin) * dt;
                        // spacing follows the local march step (iterations per segment <= SLAB_SAMPLES);
                        // a face-on full-cover crossing still integrates to the old single plane sample
                        float spacing = dt / float(SLAB_SAMPLES);
                        float weight = spacing * 1.5 / SLAB_THICKNESS;
                        [loop]
                        for (int q = 0; q < SLAB_SAMPLES + 2; ++q) {
                            if (slabNext >= slabDist + L)
                                break;
                            float k = kMin + ((slabNext - slabDist) / L) * (kMax - kMin);
                            float3 hit = lerp(pPrev, p, k);
                            float4 disk = SampleDisk(hit - bhCenter, tangent, bitangent, length(hit));
                            col += trans * disk.rgb * weight;
                            trans *= exp(-disk.a * SLAB_DENSITY * weight * DISK_OPACITY);
                            slabNext += spacing;
                        }
                        slabDist += L;
                    }

                    if (trans < 0.01)
                        break;

                    float3 away = p - bhCenter;
                    if (((dot(dir, away)) > 0.0) && (length(away) > outerDiskRad * 1.5)) {
                        break;
                    }
                }

                if (!captured)
                    col += trans * SampleLod(sky, s0, dir, 0.0).rgb;
                // streaks fly in front of the hole -> composited last, from the unflipped view direction
                float star = StarStreaks(normalize(i.viewDirection));
                col = lerp(col, float3(1.0, 1.0, 0.97) * STAR_BRIGHTNESS, star);
                return float4(col, 1.0);
            }
        )",
        ShaderDataLayout(VtxAttrs, 1)
    );
    return source;
}

// =================================================================================================
