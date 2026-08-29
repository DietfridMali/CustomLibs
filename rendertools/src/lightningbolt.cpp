#include <cmath>
#include <algorithm>
#include "lightningbolt.h"
#include "lightningnoise.h"
#include "random.hpp"

// Y-fork look (2026-07-23): 1 = forks sit preferentially at the parent's bends and the branch leaves to
// the OPPOSITE side of the bend (Y opening like real lightning); needs a reference build at the fixed
// spawn phase on animated strikes so the fork structure stays frame-stable.
// 0 = uniform fork spacing + golden-angle fan azimuth (the pre-Y-fork behavior).
#define CONTROLLED_FORKS 1

// Variable segment length (2026-07-23): 1 = the segment length scales with the bolt length around a
// calibration length X = referenceSamples * boltSegmentLength (48 * 0.25 = 12 wu): a bolt of length Y gets
// round(Y/X) * 48 samples (long bolts get proportionally coarser steps), short bolts (Y < X) coarsen by an
// integer divisor of 48 so the step never drops below minSegmentLength. Kink cell size AND kink amplitude
// scale with the local step -> every bolt is a scaled copy of the same pattern (self-similar, with a
// minimum granularity). 0 = world-fixed step: every bolt samples the same boltSegmentLength raster.
#define VARIABLE_SEGLENGTH 1

// Tortuosity diagnosis (2026-07-23): 1 = once per strike (first Generate only, not every animation frame)
// measure the distribution of the segment-to-segment deflection angles over all bolts and print it to
// stderr. Yardstick from high-speed camera statistics: >70% of the step deflections lie below 30 deg,
// lognormal shape (many small kinks, rare big outliers). Tune waveRatio/gain until the print matches.
// Purely diagnostic -- no visual change. 0 = off.
#ifdef _DEBUG
#   define TORTUOSITY 1
#else
#   define TORTUOSITY 0
#endif
#if TORTUOSITY
#   include <cstdio>
#endif

// =================================================================================================

LightningLook& LightningLook::Instance(void) {
    static LightningLook look;
    return look;
}

// =================================================================================================

namespace {
    constexpr int32_t MinSegments = 6;
    constexpr int32_t MaxSegments = 2048;   // nodes scale with length (100 wu trunk ~ 1000 nodes); GPU buffer grows on demand

    constexpr float   Pi = 3.14159265f;
    constexpr float   DegToRad = 0.01745329252f;
    constexpr float   TwoPi = 6.28318530718f;
    constexpr float   GoldenAngle = 2.39996323f;   // ~137.5 deg -> successive branches spread evenly, never bunched in one plane
    constexpr float   AnimStartSpread = 64.0f;   // random per-bolt offset range on the noise time axis, so freshly spawned bolts don't all start identical

    // segment count from a bolt length (shared by ComputeCounts and the strike's branches).
#if VARIABLE_SEGLENGTH
    // User scheme: calibration length X = referenceSamples * boltSegmentLength. Y >= X: round(Y/X) blocks
    // of 48 samples, step = Y/(blocks*48) -> the step grows with Y inside a block bracket and snaps back at
    // the next block, staying within [0.75, 1.5] * boltSegmentLength overall. Y < X: coarsen by an integer
    // divisor of 48 (48/d samples) so the step never drops below minSegmentLength -> short branches get
    // FEWER, not finer, features (minimum granularity) instead of oversampling the kink raster.
    int32_t SegmentCount(float length) {
        const LightningLook& look = lightningLook;
        float referenceLength = float(look.referenceSamples) * look.boltSegmentLength;
        if (referenceLength < 1e-4f)
            referenceLength = 1.0f;
        if (length >= referenceLength) {
            int32_t blocks = int32_t(length / referenceLength + 0.5f);   // floor below x.5, ceil above (user spec)
            if (blocks < 1)
                blocks = 1;
            int32_t n = blocks * look.referenceSamples;
            if (n > MaxSegments)
                n = MaxSegments;
            return n;
        }
        // integer-divisor quotients of 48, descending; pick the largest sample count whose step stays >= minSegmentLength
        constexpr int32_t quotients[] = { 48, 24, 16, 12, 8, 6, 4, 3, 2 };
        int32_t maxSamples = int32_t(length / ((look.minSegmentLength > 1e-4f) ? look.minSegmentLength : 1e-4f));
        for (int32_t q : quotients) {
            if (q <= maxSamples)
                return q;
        }
        return 2;
    }
#else
    int32_t SegmentCount(float length) {
        const LightningLook& look = lightningLook;
        int32_t n = int32_t(length / ((look.boltSegmentLength > 1e-4f) ? look.boltSegmentLength : 1e-4f) + 0.5f);
        if (n < MinSegments)
            n = MinSegments;
        if (n > MaxSegments)
            n = MaxSegments;
        return n;
    }
#endif

    // deterministic hash -> [0,1) from (a, b). A strike draws its branch structure from this (seeded on the
    // bolt seed + node index) instead of the global Random, so the structure is identical every frame -- only
    // the displacement (noise time axis) moves, and the branches waber in place instead of jumping around.
    float Hash01(uint32_t a, uint32_t b) {
        uint32_t h = a * 747796405u + b * 2891336453u;
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return float(h >> 8) * (1.0f / 16777216.0f);
    }

    // One lateral swing direction for the current noise sample. `axis` is the normalized bolt axis,
    // `planeDir` the precomputed in-plane direction for smPlane (see BuildPlaneDir).
    Vector3f SwingVector(const Vector3f& noiseVec, const Vector3f& axis, const Vector3f& planeDir, eSwingMode mode) {
        switch (mode) {
            case smPerpendicular:
                // the general case: strip the axis component, whatever the bolt's orientation is
                return noiseVec - axis * noiseVec.Dot(axis);
            case smPlane:
                // one single lateral direction, signed by the noise -> the path stays inside the plane
                return planeDir * noiseVec.x;
            case smHorizontal:
            default:
                // world xz only: node.y stays == base.y -> y is strictly monotone along the bolt and no
                // segment can ever run upward (gravity worlds).
                return Vector3f(noiseVec.x, 0.0f, noiseVec.z);
        }
    }

    // smPlane: the lateral direction inside the plane = normalize (planeNormal x axis). Degenerates when the
    // plane normal is parallel to the bolt -- then any perpendicular of the axis will do.
    Vector3f BuildPlaneDir(const Vector3f& planeNormal, const Vector3f& axis) {
        Vector3f dir = planeNormal.Cross(axis);
        float l = dir.Length();
        if (l > 1e-4f)
            return dir * (1.0f / l);
        Vector3f ref = (std::fabs(axis.y) > 0.9f) ? Vector3f(1.0f, 0.0f, 0.0f) : Vector3f(0.0f, 1.0f, 0.0f);
        dir = ref.Cross(axis);
        l = dir.Length();
        return (l > 1e-4f) ? dir * (1.0f / l) : Vector3f(1.0f, 0.0f, 0.0f);
    }

#if TORTUOSITY
    // Distribution of the segment-to-segment deflection angles over all bolts of one strike -> stderr.
    // 1-deg histogram internally (percentiles from the cumulative sum, no sorting needed), printed as
    // summary + 10-deg buckets. Target: p70 < 30 deg, right-skewed (lognormal-ish) shape.
    void MeasureTortuosity(const AutoArray<LightningBolt>& bolts) {
        constexpr int32_t MaxDeg = 180;
        int32_t histogram[MaxDeg + 1] = { 0 };
        int32_t n = 0;
        float sum = 0.0f;
        for (int32_t b = 0; b < bolts.Length(); b++) {
            const AutoArray<LightningNode>& nodes = bolts[b].m_nodes;
            for (int32_t i = 1; i < nodes.Length() - 1; i++) {
                Vector3f inDir = (nodes[i].position - nodes[i - 1].position).Normal();
                Vector3f outDir = (nodes[i + 1].position - nodes[i].position).Normal();
                float d = inDir.Dot(outDir);
                if (d > 1.0f)
                    d = 1.0f;
                else if (d < -1.0f)
                    d = -1.0f;
                float deg = std::acos(d) / DegToRad;
                int32_t bucket = int32_t(deg);
                if (bucket > MaxDeg)
                    bucket = MaxDeg;
                histogram[bucket]++;
                sum += deg;
                n++;
            }
        }
        if (n == 0)
            return;
        auto percentile = [&](float p) {
            int32_t target = int32_t(p * float(n) + 0.5f);
            int32_t cum = 0;
            for (int32_t deg = 0; deg <= MaxDeg; deg++) {
                cum += histogram[deg];
                if (cum >= target)
                    return deg;
            }
            return MaxDeg;
        };
        int32_t below30 = 0;
        for (int32_t deg = 0; deg < 30; deg++)
            below30 += histogram[deg];
        fprintf(stderr, "tortuosity: n=%d mean=%.1f p50=%d p70=%d p90=%d below30=%.1f%% (target: p70<30, right-skewed)\n",
                n, sum / float(n), percentile(0.5f), percentile(0.7f), percentile(0.9f), float(below30) * 100.0f / float(n));
        fprintf(stderr, "  deg:");
        int32_t above90 = n;
        for (int32_t b0 = 0; b0 < 90; b0 += 10) {
            int32_t c = 0;
            for (int32_t deg = b0; deg < b0 + 10; deg++)
                c += histogram[deg];
            above90 -= c;
            fprintf(stderr, " %d-%d=%.1f%%", b0, b0 + 10, float(c) * 100.0f / float(n));
        }
        fprintf(stderr, " 90+=%.1f%%\n", float(above90) * 100.0f / float(n));
    }
#endif
}

// =================================================================================================

void LightningBolt::Clear(void) {
    m_nodes.Clear();
    m_visibleNodes = 0;
}


void LightningBolt::Build(const LightningBoltParams& params) {
    Clear();
    const LightningLook& look = lightningLook;
    Vector3f delta = params.end - params.start;
    float length = delta.Length();
    if (length < 1e-4f)
        return;
    int32_t segments = params.segments;
    if (segments < 1)
        segments = 1;

    // Free tail: the polyline is built past the intended end and only the part up to it is drawn. The sin
    // window therefore does NOT close at the visible tip -> it stays displaced and dances (what d2x-xl did
    // with its extra invisible nodes). tailFraction 0 = both endpoints pinned, the classic behavior.
    float tail = (params.tailFraction > 0.0f) ? params.tailFraction : 0.0f;
    int32_t totalSegments = int32_t(float(segments) * (1.0f + tail) + 0.5f);
    if (totalSegments < segments)
        totalSegments = segments;
    if (totalSegments > MaxSegments)
        totalSegments = MaxSegments;
    float extent = float(totalSegments) / float(segments);   // total length as a multiple of the visible length
    Vector3f fullDelta = delta * extent;
    float fullLength = length * extent;
    Vector3f axis = delta * (1.0f / length);
    Vector3f planeDir = (params.swingMode == smPlane) ? BuildPlaneDir(params.planeNormal, axis) : Vector3f::ZERO;

    // kink-layer noise scale: input coordinate = world arc position / kinkScale, so the FINEST kink octave
    // has cells of exactly one node step -> one sample per finest cell.
#if VARIABLE_SEGLENGTH
    // cell size and kink amplitude follow the LOCAL step -> every bolt is a scaled copy of the same
    // pattern (corner sharpness ratio preserved); kinkAmplitude keeps its meaning "kink peak at the
    // calibration step boltSegmentLength".
    const float stepLength = fullLength / float(totalSegments);
    const float kinkAmplitude = look.kinkAmplitude * (stepLength / ((look.boltSegmentLength > 1e-4f) ? look.boltSegmentLength : 1e-4f));
#else
    const float stepLength = look.boltSegmentLength;      // world-fixed raster (node step == cell by SegmentCount)
    const float kinkAmplitude = look.kinkAmplitude;
#endif
    const float kinkScale = stepLength * std::pow(look.lacunarity, float(look.kinkOctaves - 1));

    m_nodes.Reserve(totalSegments + 1);
    for (int32_t i = 0; i <= totalSegments; i++) {
        float t = float(i) / float(totalSegments);
        Vector3f base = params.start + fullDelta * t;
        float window = std::sin(Pi * t);                                  // 0 at both ends of the FULL span -> anchored envelope
        float x = t * float(params.waveCount);   // length axis only; `time` is a SEPARATE 2nd noise axis (below)
        // PATH layer -- fractal (fbm) swing: a normalized direction times a fractal magnitude |fbm| in [0,1],
        // so the peak lateral swing is exactly `amplitude` and the wobble is multi-octave. Direction and
        // magnitude use independent fbm channels. `time` is the 2nd noise axis -> advancing it morphs the
        // bolt in place (animation) instead of sliding the shape sideways as `x + time` did.
        Vector3f fbmVec = LightningNoise::Fbm2Dv3(x, params.time, params.seed, look.octaves, look.gain, look.lacunarity);
        Vector3f perp = SwingVector(fbmVec, axis, planeDir, params.swingMode);
        float pl = perp.Length();
        Vector3f dir = (pl > 1e-5f) ? perp * (1.0f / pl) : Vector3f(0.0f, 0.0f, 0.0f);   // normalized swing direction
        float mag = std::fabs(LightningNoise::Fbm2D(x, params.time, params.seed ^ 0x68bc21ebu, look.octaves, look.gain, look.lacunarity));
        Vector3f disp = dir * (mag * params.amplitude * window);
        // KINK layer -- world-fixed fine jaggedness, tiled along the bolt: sampled once per finest cell
        // (see kinkScale), so consecutive nodes get ~independent values -> hard corners of world-constant
        // size and spacing on every bolt, trunk and branchlet alike. Same swing convention and window as
        // the path layer (endpoints stay pinned); own seed streams.
        float s = (t * fullLength) / ((kinkScale > 1e-5f) ? kinkScale : 1e-5f);
        Vector3f kinkVec = LightningNoise::Fbm2Dv3(s, params.time, params.seed ^ 0x7f4a7c15u, look.kinkOctaves, look.gain, look.lacunarity);
        Vector3f kperp = SwingVector(kinkVec, axis, planeDir, params.swingMode);
        float kl = kperp.Length();
        if (kl > 1e-5f) {
            float kmag = std::fabs(LightningNoise::Fbm2D(s, params.time, params.seed ^ 0x94d049bbu, look.kinkOctaves, look.gain, look.lacunarity));
            disp += kperp * (kmag * kinkAmplitude * window / kl);
        }
        LightningNode* node = m_nodes.Append();
        node->position = base + disp;
        // width tapers over the VISIBLE span, so the drawn tip carries endWidth even with a free tail
        float tw = t * extent;
        if (tw > 1.0f)
            tw = 1.0f;
        node->width = params.startWidth + (params.endWidth - params.startWidth) * tw;
    }
    m_visibleNodes = segments + 1;
}

// =================================================================================================

void BaseLightning::SetupCommon(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params) {
    m_start = start;
    m_end = end;
    m_startWidth = params.startWidth;
    m_endWidth = params.endWidth;
    m_coreWidth = params.coreWidth;
    m_color = params.color;
    m_amplitudeFactor = params.amplitudeFactor;
    m_waveRatio = params.waveRatio;
    m_swingMode = params.swingMode;
    m_planeNormal = params.planeNormal;
    m_useElevationCap = params.useElevationCap;
    m_tailFraction = params.tailFraction;
    m_regenInterval = params.regenInterval;
    m_animSpeed = params.animSpeed;
    m_timeOffset = Random::Float(AnimStartSpread);
}


void BaseLightning::ComputeCounts(void) {
    float length = CurrentLength();
    if (length < 1e-4f)
        length = 1.0f;
    m_segments = SegmentCount(length);
    // wavelength is coupled to the amplitude (= waveRatio * amplitude). Since amplitude = length * factor,
    // the base wave count is length-invariant -> trunk and every branch share it (self-similar look).
    float denom = m_waveRatio * m_amplitudeFactor;
    m_waveCount = (denom > 1e-4f) ? int32_t(1.0f / denom + 0.5f) : 1;
    if (m_waveCount < 1)
        m_waveCount = 1;
}


bool BaseLightning::Regenerate(int64_t now) {
    if (not IsAnimated())
        return false;
    if ((m_regenInterval > 0.0f) and (float(now - m_lastGenerated) < m_regenInterval))
        return false;
    m_lastGenerated = now;
    Generate(now);
    return true;
}

// =================================================================================================
// LightningStrike

void LightningStrike::Setup(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params, int64_t spawnTime) {
    SetupCommon(start, end, params);
    m_lifetime = params.lifetime;
    m_fadeStart = params.fadeStart;
    m_branchDepth = params.branchDepth;
    m_branchChance = params.branchChance;
    m_maxBranchTestSkips = params.maxBranchTestSkips;
    m_spawnTime = spawnTime;
    m_lastGenerated = spawnTime;
    m_seed = uint32_t(spawnTime) * 2654435761u + uint32_t(Random::Int(65536));
    ComputeCounts();
    Generate(spawnTime);
}


void LightningStrike::Generate(int64_t now) {
    m_bolts.Clear();
    float time = m_timeOffset + float(now - m_spawnTime) * 0.001f * m_animSpeed;   // relative to spawn -> small noise coord (float precision)
    AddBolt(m_start, m_end, m_startWidth, m_endWidth, 0, m_seed, time);
#if TORTUOSITY
    if (now == m_spawnTime)   // once per strike, not on every animation rebuild
        MeasureTortuosity(m_bolts);
#endif
}


void LightningStrike::AddBolt(const Vector3f& start, const Vector3f& end, float startWidth, float endWidth, int32_t depth, uint32_t seed, float time) {
    const LightningLook& look = lightningLook;
    Vector3f delta = end - start;
    float length = delta.Length();
    if (length < 1e-4f)
        return;
    int32_t segments = SegmentCount(length);
    int32_t waveCount = m_waveCount;   // coupled + length-invariant -> same base wiggle count for trunk & branches
    float amplitude = length * m_amplitudeFactor;

    LightningBoltParams boltParams = {
        .start = start,
        .end = end,
        .segments = segments,
        .waveCount = waveCount,
        .startWidth = startWidth,
        .endWidth = endWidth,
        .amplitude = amplitude,
        .seed = seed,
        .time = time,
        .swingMode = m_swingMode,
        .planeNormal = m_planeNormal,
        .tailFraction = m_tailFraction
    };
    LightningBolt bolt;
    bolt.Build(boltParams);
    // forks may only sit on the DRAWN part of the polyline -- a branch off the invisible free tail would
    // hang in mid-air with no trunk leading to it.
    int32_t nodeCount = bolt.VisibleNodes();
    if (nodeCount < 2)
        return;
    m_bolts.Append(bolt);

    if (depth >= m_branchDepth)
        return;

#if CONTROLLED_FORKS
    // Branch decisions must stay a function of the seed alone (contract: the structure is fixed across
    // frames, only `time` animates the displacement). The kink metric below reads geometry, so an animated
    // strike evaluates it on a reference build at the fixed spawn phase (m_timeOffset) -- otherwise forks
    // would pop between nodes as the path wabers. A static strike (time == m_timeOffset) reuses the live bolt.
    LightningBolt refBolt;
    const LightningBolt* structure = &bolt;
    if (time != m_timeOffset) {
        LightningBoltParams refParams = boltParams;
        refParams.time = m_timeOffset;
        refBolt.Build(refParams);
        structure = &refBolt;
    }
    // Per-node kink angle (between incoming and outgoing segment) of the structure path. Real lightning
    // branches where the channel bends -- branching and tortuosity are one mechanism -- so the kink drives
    // both WHERE forks happen (accumulation weight, normalized to mean 1 so the long-term rate stays
    // branchChance) and to WHICH SIDE the branch leaves (opposite side of the bend, below).
    float kinks[MaxSegments + 1];
    float kinkSum = 0.0f;
    for (int32_t i = 1; i < nodeCount - 1; i++) {
        Vector3f inDir = (structure->m_nodes[i].position - structure->m_nodes[i - 1].position).Normal();
        Vector3f outDir = (structure->m_nodes[i + 1].position - structure->m_nodes[i].position).Normal();
        float d = inDir.Dot(outDir);
        if (d > 1.0f)
            d = 1.0f;
        else if (d < -1.0f)
            d = -1.0f;
        kinks[i] = std::acos(d);
        kinkSum += kinks[i];
    }
    float meanKink = (nodeCount > 2) ? kinkSum / float(nodeCount - 2) : 0.0f;
    if (meanKink < 1e-5f)
        meanKink = 1e-5f;
#endif

    Vector3f up = Vector3f(0.0f, 1.0f, 0.0f);
    // golden-angle fan azimuth (CONTROLLED_FORKS: fallback only, where the parent runs locally straight and
    // has no bend side). Seeded hash, NOT the global Random -- an animated strike regenerates every frame,
    // so the fan orientation must be deterministic.
    float baseAzimuth = Hash01(seed ^ 0x27d4eb2fu, 0u) * TwoPi;
    int32_t branchIndex = 0;
    // elevation cap for every branch forked off this bolt (see the constants above; delta/length = this
    // bolt's axis). Only meaningful in a world with a global "up" -- a 6DoF world switches it off.
    float elevationCap = 0.0f;
    if (m_useElevationCap) {
        // The sine is in [-1,1] by construction -- clamped anyway, float rounding can nudge it past 1.
        float axisSine = delta.y / length;
        if (axisSine > 1.0f)
            axisSine = 1.0f;
        else if (axisSine < -1.0f)
            axisSine = -1.0f;
        float parentElevation = std::asin(axisSine);
        elevationCap = parentElevation + look.maxBranchElevationRise * DegToRad;
        if (elevationCap > look.maxBranchElevation * DegToRad)
            elevationCap = look.maxBranchElevation * DegToRad;
    }
    float accumChance = 0.0f;
    int32_t skipBranchTests = 0;
    for (int32_t i = 1; i < nodeCount - 1; i++) {
        float remaining = length * (1.0f - float(i) / float(nodeCount - 1));
        // length variation from a seeded hash (own stream via seed xor -- the i*3u streams below stay
        // untouched), NOT the global Random: an animated strike regenerates every frame, so the lengths
        // must be as deterministic as the branch structure or the branches jitter/flicker per frame.
        float branchLength = remaining * (look.branchLengthFactor + float(int32_t(Hash01(seed ^ 0x9e3779b9u, uint32_t(i)) * 4.0f)) * 0.1f);
        if (branchLength < look.minBranchLength)
            continue;
        // accumulate branchChance at every node and only test once the skip window has run out; on a hit,
        // consume one whole unit of chance and open a fresh random skip window -> branches come guaranteed-but-spaced.
#if CONTROLLED_FORKS
        // Kink-weighted (mean weight 1): the chance piles up fastest at the strong bends, so that's where
        // the test tends to fire -> forks sit at the kinks.
        accumChance += m_branchChance * (kinks[i] / meanKink);
#else
        accumChance += m_branchChance;
#endif
        if (--skipBranchTests > 0)
            continue;
        skipBranchTests = 0;
        if (Hash01(seed, uint32_t(i) * 3u) >= accumChance)   // seeded (deterministic) -> branch structure identical every frame
            continue;
        accumChance -= 1.0f;
        skipBranchTests = int32_t(Hash01(seed, uint32_t(i) * 3u + 1u) * float(m_maxBranchTestSkips)) + 1;
        // branch deflection off the parent's LOCAL tangent at the fork node: normal draw (Box-Muller from
        // two independent hash streams; the second one is seed-xored so it cannot collide with the i*3u
        // streams of the next node), clamped.
#if CONTROLLED_FORKS   // structure path -> frame-stable branch axes; anchors follow the live bolt
        Vector3f tangent = (structure->m_nodes[i + 1].position - structure->m_nodes[i - 1].position).Normal();
#else
        Vector3f tangent = (bolt.m_nodes[i + 1].position - bolt.m_nodes[i - 1].position).Normal();
#endif
        float u1 = Hash01(seed, uint32_t(i) * 3u + 2u);
        if (u1 < 1e-6f)
            u1 = 1e-6f;
        float u2 = Hash01(seed ^ 0x85ebca6bu, uint32_t(i));
        float angleDeg = look.branchAngleMean + std::sqrt(-2.0f * std::log(u1)) * std::cos(TwoPi * u2) * look.branchAngleSigma;
        if (angleDeg < look.minBranchAngle)
            angleDeg = look.minBranchAngle;
        else if (angleDeg > look.maxBranchAngle)
            angleDeg = look.maxBranchAngle;
        float angle = angleDeg * DegToRad;
        Vector3f ref = (std::fabs(tangent.Dot(up)) > 0.9f) ? Vector3f(1.0f, 0.0f, 0.0f) : up;
        Vector3f u = tangent.Cross(ref).Normal();
        Vector3f v = tangent.Cross(u);
        float azimuth;
#if CONTROLLED_FORKS
        // azimuth: opposite side of the parent's local bend -> the branch visually continues the incoming
        // direction while the parent kinks away = the Y fork of real lightning (the surviving channel
        // deflects ~22-30 deg, the weak branch ~45 to the other side, included angle ~60). Jittered so the
        // forks don't look machined; golden-angle fan only where the parent runs locally straight.
        Vector3f inDir = (structure->m_nodes[i].position - structure->m_nodes[i - 1].position).Normal();
        Vector3f outDir = (structure->m_nodes[i + 1].position - structure->m_nodes[i].position).Normal();
        Vector3f bend = outDir - inDir;
        bend -= tangent * tangent.Dot(bend);   // bend component in the plane perpendicular to the tangent
        float bl = bend.Length();
        if (bl > 1e-4f)
            azimuth = std::atan2(-bend.Dot(v), -bend.Dot(u))
                    + (Hash01(seed ^ 0xc2b2ae35u, uint32_t(i)) - 0.5f) * (2.0f * look.branchAzimuthJitter * DegToRad);
        else
            azimuth = baseAzimuth + float(branchIndex) * GoldenAngle;
#else
        // 3D fan: golden-angle azimuth around the tangent -> successive branches spread evenly in space
        azimuth = baseAzimuth + float(branchIndex) * GoldenAngle;
#endif
        branchIndex++;
        Vector3f perpDir = u * std::cos(azimuth) + v * std::sin(azimuth);
        Vector3f branchDir = tangent * std::cos(angle) + perpDir * std::sin(angle);
        // elevation cap: if the net direction points higher than the parent axis allows, rotate branchDir
        // down around the fork node inside its own vertical plane (azimuth preserved) onto the cap. The cap
        // may itself be negative (steep trunk) -> the branch still slants downward there.
        if (m_useElevationCap and (branchDir.y > std::sin(elevationCap))) {
            Vector3f h = Vector3f(branchDir.x, 0.0f, branchDir.z);
            float hl = h.Length();
            if (hl < 1e-4f) {   // branch near straight up: keep the fan's azimuth via perpDir's horizontal part
                h = Vector3f(perpDir.x, 0.0f, perpDir.z);
                hl = h.Length();
                if (hl < 1e-4f) {
                    h = Vector3f(1.0f, 0.0f, 0.0f);
                    hl = 1.0f;
                }
            }
            branchDir = h * (std::cos(elevationCap) / hl) + up * std::sin(elevationCap);
        }
        Vector3f branchEnd = bolt.m_nodes[i].position + branchDir * branchLength;
        uint32_t branchSeed = seed * 747796405u + uint32_t(i) * 2891336453u + uint32_t(depth + 1) * 0x9e3779b9u;
        float widthFactor = (depth == 0) ? look.firstBranchWidthFactor : look.deepBranchWidthFactor;   // secondaries drop hard, deeper ones barely
        AddBolt(bolt.m_nodes[i].position, branchEnd, bolt.m_nodes[i].width * widthFactor, 0.0f, depth + 1, branchSeed, time);
    }
}


bool LightningStrike::IsAlive(int64_t now) const {
    return float(now - m_spawnTime) * 0.001f < m_lifetime;
}


float LightningStrike::Fade(int64_t now) const {
    const LightningLook& look = lightningLook;
    if (m_lifetime < 1e-4f)
        return 0.0f;
    float age = float(now - m_spawnTime) * 0.001f;
    float fadeSpan = m_fadeStart;   // param is in ms before the end of lifetime
    if (fadeSpan > m_lifetime)
        fadeSpan = m_lifetime;               // fade window can't start before spawn
    float fadeBegin = m_lifetime - fadeSpan;
    float decay;
    if ((fadeSpan < 1e-4f) || (age <= fadeBegin))   // no fade window, or still in the hold phase
        decay = 1.0f;
    else
        decay = powf(std::clamp(1.0f - (age - fadeBegin) / fadeSpan, 0.0f, 1.0f), 4.0f);
    uint32_t bucket = uint32_t(age * look.flickerRate);
    uint32_t h = (bucket + uint32_t(m_spawnTime)) * 2654435761u;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 13;
    float r = float(h & 0xffffu) * (1.0f / 65535.0f);
    return decay * (look.flickerFloor + (1.0f - look.flickerFloor) * r);
}


// AFTERGLOW: reports whether the ttl fade window has begun. Mirrors the window math in Fade() above
// (m_fadeStart's unit handling included) -- keep the two in sync. While this returns true the handler
// skips the strike in the full-res core pass, so only the blurred halo remains and fades out.
bool LightningStrike::IsFading(int64_t now) const {
    float fadeSpan = m_fadeStart;
    if (fadeSpan > m_lifetime)
        fadeSpan = m_lifetime;
    if (fadeSpan < 1e-4f)
        return false;
    float age = float(now - m_spawnTime) * 0.001f;
    return age > m_lifetime - fadeSpan;
}


void LightningStrike::UpdateEndpoints(const Vector3f& /*start*/, const Vector3f& end) {
    m_end = end;
    if (m_bolts.Length() < 1)
        return;
    LightningBolt& main = m_bolts[0];               // [0] is the trunk; [1..] are branches, left untouched
    int32_t n = main.VisibleNodes();
    // A free tail means the tip is deliberately NOT pinned -- moving it onto the target would defeat that.
    if ((n >= 1) and (m_tailFraction <= 0.0f))
        main.m_nodes[n - 1].position = end;         // tip node carries no noise (window=0) -> this just re-aims the end segment
}

// =================================================================================================
// LightningArc

void LightningArc::Setup(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params) {
    SetupCommon(start, end, params);
    m_endWidth = params.startWidth;   // arc: uniform width (endWidth ignored) -- unchanged behavior
    m_boltCount = (params.boltCount < 1) ? 1 : params.boltCount;
    m_seed = uint32_t(Random::Int(0x7fffffff));
    ComputeCounts();
    Generate(0);
}


void LightningArc::Generate(int64_t now) {
    m_bolts.Clear();
    float length = CurrentLength();
    if (length < 1e-4f)
        return;
    float amplitude = length * m_amplitudeFactor;                 // relative amplitude -> auto-scales with length
    float time = m_timeOffset + float(now) * 0.001f * m_animSpeed;   // random start offset so different arcs don't look identical
    for (int32_t b = 0; b < m_boltCount; b++) {
        LightningBoltParams boltParams = {
            .start = m_start,
            .end = m_end,
            .segments = m_segments,
            .waveCount = m_waveCount,
            .startWidth = m_startWidth,
            .endWidth = m_endWidth,
            .amplitude = amplitude,
            .seed = m_seed + uint32_t(b) * 2654435761u,
            .time = time + float(b) * 1.7f,
            .swingMode = m_swingMode,
            .planeNormal = m_planeNormal,
            .tailFraction = m_tailFraction
        };
        LightningBolt bolt;
        bolt.Build(boltParams);
        m_bolts.Append(bolt);
    }
}

// =================================================================================================
