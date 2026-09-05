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

// -------------------------------------------------------------------------------------------------
// BlackholeShader: gravitationally lensed accretion disk over a sky cubemap (direction-only).
// The view ray is bent through the hole's field so the disk's far side wraps above and below the
// event horizon; disk structure is sampled from the shared 3D cloud shape-noise volume.
// Uniforms: mView, direction, distance, diskNormal, gravity, time, horizon, innerDiskRad, outerDiskRad,
//           angSpeed, brightness, noiseScale.
const ShaderSource& BlackholeShader() {
    static const ShaderSource source(
        "blackhole",
        String(R"(
            #version 330
            layout(location=0) in vec3 position;
            uniform mat4 mProjection;
            uniform mat4 mView;
            out vec3 viewDirection;
            void main() {
                viewDirection = position;
                gl_Position = mProjection * mView * vec4(position,1.0);
                gl_Position = gl_Position.xyww;
            }
        )"),
        String(R"(
            #version 330
            in vec3 viewDirection;
            uniform mat4 mView;
            uniform samplerCube sky;
            uniform sampler3D noiseTex;
            uniform sampler3D blueNoiseTex;
            uniform vec3 direction;
            uniform float distance;
            uniform vec3 diskNormal;
            uniform float gravity;
            uniform float time;
            uniform float horizon;
            uniform float innerDiskRad;
            uniform float outerDiskRad;
            uniform float angSpeed;
            uniform float brightness;
            uniform float noiseScale;
            out vec4 fragColor;

            const int   MAX_STEPS    = 200;
            const float STEP_MIN     = 0.02;
            const float STEP_MAX     = 0.60;
            const float DISK_OPACITY = 1.0;
            const float PI           = 3.141592653589793;
            const float TAU           = 6.28318530718;
            const float TANG_PERIODS  = 2.0;
            const float RAD_SCALE     = 0.5;
            const float SPIRAL_TWIST  = 4.0;
            const float MORPH_SPEED   = 0.02;
            const float DUST_STRENGTH = 0.7;
            const float SLAB_THICKNESS   = 0.7;   // full disk thickness [wu]
            const int   SLAB_SAMPLES     = 3;     // noise samples per march segment inside the slab
            const float SLAB_DENSITY     = 4.0;   // optical depth of a face-on crossing at full cover
            const float CLUMP_PERIODS    = 14.0;  // tangential periods of the outer clump layer (integer!)
            const float CLUMP_WAVELEN    = 3.0;   // world-space clump size [wu]
            const float CLUMP_BASE       = 0.15;  // density floor between clumps (gap depth)
            const float CLUMP_START      = 4.5;   // clump layer fades in from this radius ...
            const float CLUMP_FULL       = 6.5;   // ... and is fully active from here on
            const float SPIRAL_RMAX      = 5.0;   // spiral shear saturates beyond this radius
            const float STAR_SECTORS    = 24.0;
            const int   STAR_LAYERS     = 6;
            const float STAR_LEN        = 0.0075;
            const float STAR_WIDTH      = 0.0025;
            const float STAR_SPEED      = 0.25;
            const float STAR_BRIGHTNESS = 1.5;
            const float STAR_FIELD_ANG  = 1.7;  // angular field radius [rad], independent of hole distance
#if 1
            float Amp(float v) {
                return 0.5 - 0.5 * cos(v * PI);
            }

            float InvAmp(float v) {
                return 0.5 + 0.5 * cos(v * PI);
            }

            float Amp2(float v) {
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

            vec3 DiskColor(float t) {
                vec3 cool = vec3(1.00, 0.42, 0.16);
                vec3 mid  = vec3(1.00, 0.72, 0.40);
                vec3 hot  = vec3(1.00, 0.97, 0.92);
                vec3 c = mix(cool, mid, smoothstep(0.0, 0.5, t));
                return mix(c, hot, smoothstep(0.5, 1.0, t));
            }

            float DiskNoise(vec3 p, float lod) {
                float n  = textureLod(noiseTex, p, lod).r;
                n += 0.50 * textureLod(noiseTex, p * 2.03 + vec3(1.7, 0.0, 0.3), lod).r;
                n += 0.25 * textureLod(noiseTex, p * 4.07 + vec3(0.0, 2.3, 1.1), lod).r;
                return n * 0.5714;
            }

            float Hash(float v) {
                return fract(sin(v * 12.9898) * 43758.5453);
            }

            // infalling stars: elongated white streaks with a soft gaussian fringe, accelerating
            // toward the hole; laid out in angular sectors around the hole direction, so each pixel
            // only evaluates the streaks of its own sector. Radial coordinate is the true angle to
            // the hole (not gnomonic), so the field radius STAR_FIELD_ANG can exceed 90 degrees and
            // stars always spawn at the far edge of the view, regardless of hole distance.
            float StarStreaks(vec3 viewDir) {
                vec3  bhDir = normalize(direction);
                float ang = acos(clamp(dot(viewDir, bhDir), -1.0, 1.0));
                float angStop = 1.6 * (horizon / distance);
                if ((ang < angStop * 0.5) || (ang > STAR_FIELD_ANG))
                    return 0.0;
                vec3  up = (abs(bhDir.y) > 0.99) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
                vec3  e1 = normalize(cross(up, bhDir));
                vec3  e2 = cross(bhDir, e1);
                float cellWidth = TAU / STAR_SECTORS;
                float phi = atan(dot(viewDir, e2), dot(viewDir, e1));
                float cell = floor(phi / cellWidth);
                float a = 0.0;
                for (int j = 0; j < STAR_LAYERS; ++j) {
                    float fj = float(j);
                    float h0 = Hash(cell * 7.13 + fj * 37.71 + 11.0);
                    float h1 = Hash(cell * 3.77 + fj * 17.29 + 5.0);
                    float h2 = Hash(cell * 9.41 + fj * 23.63 + 2.0);
                    float s = fract(time * STAR_SPEED * (0.6 + 0.8 * h1) + h2);
                    float angStar = mix(STAR_FIELD_ANG, angStop, s * s);
                    float len = STAR_LEN * (0.6 + 0.8 * h2) * (0.5 + 2.0 * s);
                    float width = STAR_WIDTH * (0.6 + 0.8 * h0);
                    // jitter capped at half the sector so the streak never leaks into a neighbour cell
                    float starPhi = (cell + 0.5 + (h0 - 0.5) * 0.5) * cellWidth;
                    float dPar = (ang - angStar) / len;
                    float dPerp = ((phi - starPhi) * sin(ang)) / width;
                    float fade = smoothstep(0.0, 0.15, s) * (1.0 - smoothstep(0.8, 1.0, s));
                    a += fade * exp(-(dPar * dPar + dPerp * dPerp) * 4.0);
                }
                return clamp(a, 0.0, 1.0);
            }

            // point sample inside the disk slab (|height| < SLAB_THICKNESS/2 around the disk plane)
            vec4 SampleDisk(vec3 rel, vec3 tangent, vec3 bitangent, float distToCam) {
                vec3  inPlane = rel - diskNormal * dot(rel, diskNormal);
                float radius = length(inPlane);
                if ((radius < innerDiskRad) || (radius > outerDiskRad)) {
                    return vec4(0.0, 0.0, 0.0, 0.0);
                }
                float yNorm = dot(rel, diskNormal) / (0.5 * SLAB_THICKNESS);
                if (abs(yNorm) > 1.0) {
                    return vec4(0.0, 0.0, 0.0, 0.0);
                }
                float t = (radius - innerDiskRad) / (outerDiskRad - innerDiskRad);
                float tHot = 1.0 - t;

                float theta = atan(dot(inPlane, bitangent), dot(inPlane, tangent));
                float lod = clamp((distToCam - distance) / distance, 0.0, 1.0);

                // shear saturates beyond SPIRAL_RMAX so the outer structure is not dragged into
                // long arcs; the inner spiral is unchanged
                float spiral = SPIRAL_TWIST * sqrt(min(radius, SPIRAL_RMAX) / innerDiskRad);
                float phi = theta + spiral + time * angSpeed;
                float u = phi * (TANG_PERIODS / TAU);
                float v = radius * noiseScale * RAD_SCALE;
                // vertical morph offset: top and bottom of the layer sample different noise slices
                float w = time * MORPH_SPEED + yNorm * 0.15;

                float nBig = textureLod(noiseTex, vec3(u, v, w), lod).r;
                float nFine = textureLod(noiseTex, vec3(u * 3.0 + 0.37, v * 3.0 + 0.61, w * 2.0 + 0.5), lod).r;
                float dens = Amp2(nBig) * (0.15 + 0.85 * Ridged2(nFine));

                float dust = Amp2(textureLod(noiseTex, vec3(u * 2.0 + 0.71, v * 2.0 + 0.13, w * 0.7 + 0.29), lod).r);

                // outer clump layer: near-isotropic world-space blobs (~CLUMP_WAVELEN wu), rigid
                // rotation only (no spiral shear -> round puffs with real gaps between them);
                // radius-gated so the inner disk stays bit-identical
                float clumpGate = smoothstep(CLUMP_START, CLUMP_FULL, radius);
                if (clumpGate > 0.0) {
                    float uClump = (theta + time * angSpeed) * (CLUMP_PERIODS / TAU);
                    float vClump = radius / CLUMP_WAVELEN;
                    float clump = Amp2(textureLod(noiseTex, vec3(uClump, vClump, w + 0.77), lod).r);
                    float shred = mix(1.0, CLUMP_BASE + (1.0 - CLUMP_BASE) * clump, clumpGate);
                    dens *= shred;
                    dust *= shred;
                }

                float edge = smoothstep(innerDiskRad, innerDiskRad + 0.35, radius) * smoothstep(outerDiskRad, outerDiskRad - 2.0, radius);
                // parabolic vertical profile: full density in the midplane, soft top and bottom faces
                float profile = 1.0 - yNorm * yNorm;
                dens *= edge * profile;
                dust *= edge * profile;

                float bright = mix(0.30, 1.80, tHot) * brightness;
                vec3 col = DiskColor(tHot) * bright * dens;
                col *= mix(vec3(1.0, 1.0, 1.0), vec3(0.25, 0.15, 0.10), dust * DUST_STRENGTH);
                float cover = clamp(dens + dust * 0.6, 0.0, 1.0);
                return vec4(col, cover);
            }

            void main() {
                vec3 viewDir = normalize(viewDirection);
                vec3 bhCenter = direction * distance;
                vec3 n = normalize(diskNormal);

                if (dot(-bhCenter, n) < 0.0) {
                    vec3 flipAxis = n - direction * (dot(n, direction) / dot(direction, direction));
                    float flipLen = length(flipAxis);
                    if (flipLen > 1e-4) {
                        flipAxis = flipAxis / flipLen;
                        viewDir = normalize(viewDir - 2.0 * dot(viewDir, flipAxis) * flipAxis);
                    }
                }

                vec3 up = /*(abs(n.y) > 0.99) ? vec3(1.0, 0.0, 0.0) :*/ vec3(0.0, 1.0, 0.0);
                vec3 tangent = normalize(cross(up, n));
                vec3 bitangent = cross(n, tangent);

                vec3  p = vec3(0.0, 0.0, 0.0);
                vec3  dir = viewDir;
                vec3  col = vec3(0.0, 0.0, 0.0);
                float trans = 1.0;
                bool  captured = false;
                // in-slab path integration state: distance traveled inside the slab so far, and the
                // in-slab distance at which the next sample fires. Sample positions derive only from
                // the ray's slab entry (continuous in the view direction -> no step-grid banding);
                // their spacing follows the local march step, so the sample density matches the
                // adaptive march: fine near the hole, coarse outside.
                float slabDist = 0.0;
                float slabNext = 0.0;

                for (int s = 0; s < MAX_STEPS; ++s) {
                    vec3  toBH = bhCenter - p;
                    float dist = length(toBH);
                    float dt = clamp(0.12 * (dist - horizon) + 0.03, STEP_MIN, STEP_MAX);
                    dir = normalize(dir + (toBH / dist) * (gravity / (dist * dist)) * dt);
                    vec3 pPrev = p;
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
                        for (int q = 0; q < SLAB_SAMPLES + 2; ++q) {
                            if (slabNext >= slabDist + L)
                                break;
                            float k = kMin + ((slabNext - slabDist) / L) * (kMax - kMin);
                            vec3 hit = mix(pPrev, p, k);
                            vec4 disk = SampleDisk(hit - bhCenter, tangent, bitangent, length(hit));
                            col += trans * disk.rgb * weight;
                            trans *= exp(-disk.a * SLAB_DENSITY * weight * DISK_OPACITY);
                            slabNext += spacing;
                        }
                        slabDist += L;
                    }

                    if (trans < 0.01)
                        break;

                    vec3 away = p - bhCenter;
                    if (((dot(dir, away)) > 0.0) && (length(away) > outerDiskRad * 1.5)) {
                        break;
                    }
                }

                if (!captured)
                    col += trans * textureLod(sky, dir, 0.0).rgb;
                // streaks fly in front of the hole -> composited last, from the unflipped view direction
                float star = StarStreaks(normalize(viewDirection));
                col = mix(col, vec3(1.0, 1.0, 0.97) * STAR_BRIGHTNESS, star);
                fragColor = vec4(col, 1.0);
            }
        )")
    );
    return source;
}

// =================================================================================================
