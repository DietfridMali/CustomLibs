#pragma once

#include <cstdint>
#include "vector.hpp"
#include "array.hpp"

// =================================================================================================
// Generic procedural lightning. Class layers (all app-agnostic):
//   LightningBolt   - one jagged polyline (the geometry primitive).
//   BaseLightning   - a "Buschel": 1..n bolts of one appearance (width, amplitude) between two points,
//                     plus the common params. Endpoints are arbitrary; SetEndpoints moves them.
//     LightningStrike - one-shot: a main bolt + recursive branches, TTL-faded, static geometry.
//     LightningArc    - persistent: n bolts (no branches), animated (bolt path writhes within its
//                       envelope), amplitude given RELATIVE to the endpoint distance (auto-scales when
//                       the endpoints move).
// A LightningSystem groups several of these and may own a LightningEmitter that (re)ignites them over
// time; the LightningHandler (application side) owns systems by id and renders them.

// -------------------------------------------------------------------------------------------------
// Where a bolt is allowed to swing. The path displacement is always perpendicular to SOMETHING - which
// "something" depends on the world:
//   smHorizontal    - world xz plane only. For gravity worlds where bolts run top-down: node.y stays
//                     exactly on the straight line, so no segment can ever run back upward.
//   smPerpendicular - the plane perpendicular to the bolt axis. The correct choice without a global
//                     "up" (6DoF), and the only one that keeps the swing orthogonal to a bolt of any
//                     orientation.
//   smPlane         - one single lateral direction inside the plane given by planeNormal
//                     (dir = normalize (planeNormal x axis)). For discharges creeping ALONG a surface:
//                     the path stays in the surface instead of lifting off it.

enum eSwingMode {
    smHorizontal,
    smPerpendicular,
    smPlane
};

// -------------------------------------------------------------------------------------------------
// The universal look, shared by every lightning of an application. Defaults are the values tuned for
// Paintjob Rampage; an application with a different world scale (Descent's world units are not the
// arena's) overwrites what it needs once at startup via LightningLook::Instance ().
//
// Two-layer displacement: the PATH layer is the self-similar slow swing (wavelength AND amplitude
// proportional to the bolt length -> bounded flank steepness, the trunk keeps its big S-curve), the
// KINK layer is world-fixed fine jaggedness tiled along the bolt (its own small, world-fixed amplitude
// tied to its own small wavelength). Every scale respects the geometric bound steepness ~
// amplitude/wavelength SEPARATELY -- a length-scaled amplitude on a world-fixed short wave would fold
// the polyline.
//
// boltSegmentLength is BOTH the segment length and the world size of the finest kink cell -> by
// construction one sample per finest kink cell: consecutive nodes get ~independent kink values, the
// kinks read as hard corners. Node count scales with the bolt length (branch/fork density too).

struct LightningLook {
    float   boltSegmentLength{ 0.25f };  // segment count from the bolt length = finest kink cell size (wu); the step at the calibration length
    float   minSegmentLength{ 0.1f };    // step floor for short branches
    int32_t referenceSamples{ 48 };      // samples per calibration length (calibration length = referenceSamples * boltSegmentLength)

    int32_t octaves{ 3 };                // PATH layer fbm octaves: the slow self-similar swing
    float   gain{ 0.6f };                // fbm amplitude falloff per octave (both layers)
    float   lacunarity{ 2.0f };          // fbm frequency growth per octave (both layers)

    int32_t kinkOctaves{ 2 };            // KINK layer octaves
    float   kinkAmplitude{ 0.4f };       // world-fixed peak of the kink layer (wu) -- the corner sharpness knob
    // How much of the swing may leave the bolt's own plane, as a share of the in-plane swing. 0 = a
    // strictly flat bolt, 1 = the plane means nothing. A small share keeps a bolt from collapsing to
    // a straight line when it is seen edge on, without letting the path circle its axis again.
    float   planeDistTolerance{ 0.5f };

    // Branch deflection off the parent's local tangent: normal distribution (high-speed camera statistics:
    // weak branches deflect ~40-45 deg, normally distributed, sigma 14-24 deg -- the values are 2D projections,
    // so only the distribution SHAPE is authoritative, not the exact degrees), clamped to [min,max].
    float   branchAngleMean{ 45.0f };
    float   branchAngleSigma{ 18.0f };
    float   minBranchAngle{ 15.0f };
    float   maxBranchAngle{ 80.0f };
    // Elevation cap (only evaluated when BaseLightning::m_useElevationCap is set - a gravity world),
    // measured against the parent's AXIS (end-start, a stable design quantity, NOT the wobbling local
    // tangent): a branch may point at most maxBranchElevationRise steeper UPWARD than its parent axis,
    // and never above maxBranchElevation absolute. No downward cap.
    float   maxBranchElevationRise{ 55.0f };
    float   maxBranchElevation{ 25.0f };
    float   branchAzimuthJitter{ 30.0f };   // +/- jitter on the Y-fork azimuth so forks don't look machined
    float   branchLengthFactor{ 0.55f };    // branch length as a fraction of the parent's remaining length past the fork node
    float   minBranchLength{ 0.2f };
    // Branch start width, as a fraction of the parent's width at the fork node. Real lightning drops hard
    // from trunk to secondary branch, then barely thins further -- so the factor GROWS with depth.
    float   firstBranchWidthFactor{ 0.3f };
    float   deepBranchWidthFactor{ 0.9f };

    // The DEFAULT ribbon width, and it is a RATIO rather than a width: half width per unit of bolt
    // length. A width in world units cannot serve as a default across effects, because one number
    // would have to fit a 2 unit discharge around a small object AND a 20 unit bolt across a room -
    // which is exactly what went wrong before. A ratio is scale free: it survives a change of world
    // scale and it is the same number in both applications. 1 : 60 is where Paintjob Rampage's bolts
    // sit (half width 0.5 over a 30 unit strike), and that is the look this was calibrated against.
    // A bundle that states a startWidth of its own never consults this.
    float   widthRatio{ 1.0f / 60.0f };

    float   flickerRate{ 18.0f };        // strike: pseudo-random brightness levels per second
    float   flickerFloor{ 0.3f };

    static LightningLook& Instance(void);
};

#define lightningLook LightningLook::Instance()

// -------------------------------------------------------------------------------------------------
// Creation parameters, passed by designated initializer to the Create/Add routines, e.g.
//   CreateStrike(start, end, { .amplitudeFactor = 0.2f, .branchDepth = 1, .branchChance = 0.2f });
// One struct for both kinds: the strike fields drive a one-shot branched bolt, the arc fields a
// persistent animated bundle; each kind ignores the other's fields. start/end stay separate arguments
// (they get re-anchored live via UpdateStrike/SetEndpoints).

// The noise properties of a BUNDLE. Every discharge in one system shares them - they are what makes
// its bolts the same KIND of discharge, so there is one instance per system and its lightnings point
// at it (LightningSystem::m_fbm). Changing it there changes every bolt of that bundle, including the
// ones its emitter ignites later. Out of range = fall back to the application wide LightningLook.
struct LightningFbmParams {
    float   kinkAmplitude{ -1.0f };   // world-fixed peak of the KINK layer (wu) - corner sharpness
    int32_t octaves{ -1 };            // PATH layer fbm octaves - how much fine detail rides on the swing
    float   gain{ -1.0f };            // fbm amplitude falloff per octave; BOTH layers use it
    float   lacunarity{ -1.0f };      // fbm frequency growth per octave; BOTH layers use it
    int32_t kinkOctaves{ -1 };        // KINK layer octaves - detail of the fine jaggedness
    float   planeDistTolerance{ -1.0f };      // share of the swing that may leave the plane; negative = the look default
};

// -------------------------------------------------------------------------------------------------

struct LightningCreationParams {
    // shape (strike + arc)
    // Half width at the source end, in world units. 0 - the default - means DERIVE IT: the bolt takes
    // its own length times LightningLook::widthRatio, so an effect that has no opinion about width
    // still scales correctly with whatever length it happens to have.
    float      startWidth{ 0.0f };
    // Strike: half width at the tip. 0 runs to a point and is a real value, so 0 cannot mean "derive"
    // here - NEGATIVE does, and yields half of the resolved start width. Arc: ignored (uniform).
    float      endWidth{ -1.0f };
    float      coreWidth{ 0.5f };   // white-core band as a fraction of the ribbon half-width; the rest is blue halo
    Vector3f   color{ Vector3f(1.0f, 1.0f, 1.0f) };   // per-bolt tint of the halo (multiplies the shader's haloColor); white = the shader's own colour
    float      amplitudeFactor{ 0.2f };   // lateral swing as a fraction of the bolt length
    float      waveRatio{ 3.0f };   // wavelength / amplitude -> base jaggedness (coupled -> length-invariant, self-similar)
    // Noise properties. They belong to the BUNDLE, not to the single bolt - the system copies them
    // into its own LightningFbmParams and every discharge it holds reads them from there.
    LightningFbmParams fbm;
    eSwingMode swingMode{ smHorizontal };  // see eSwingMode; 6DoF worlds want smPerpendicular
    Vector3f   planeNormal{ Vector3f::ZERO };  // smPlane only: normal of the plane the bolt has to stay in
    bool       useElevationCap{ true };   // gravity world: cap how much steeper upward a branch may point than its parent
    float      tailFraction{ 0.0f };   // build this much of the length BEYOND the end and do not draw it -> the visible tip is not pinned and dances
    float      regenInterval{ 33.0f };  // ms between path rebuilds of an animated lightning (0 = every frame)
    // strike only
    float      lifetime{ 1.0f };   // seconds the strike stays (ttl-faded)
    float      fadeStart{ 150.0f }; // ms before the end of lifetime at which the brightness starts falling off (afterglow); full brightness before
    int32_t    branchDepth{ 2 };      // 0 = trunk only, 1 = trunk branches, 2 = branches branch, ...
    float      branchChance{ 0.2f };   // per-node fork probability [0,1]
    int32_t    maxBranchTestSkips{ 2 };      // after a fork, skip Random::Int(this) nodes before testing again (0 = never skip)
    // arc only
    int32_t    boltCount{ 3 };      // parallel bolts in the bundle
    float      animSpeed{ 1.0f };   // writhe speed
};

// -------------------------------------------------------------------------------------------------

struct LightningNode {
    Vector3f position;
    float    width;
};

// -------------------------------------------------------------------------------------------------
// Everything LightningBolt::Build needs. The lateral displacement is
// (swing direction) * |fbm| * amplitude * sin(pi*t) -- a fractal (multi-octave) swing whose peak is
// `amplitude`. The sin window pins both endpoints (the envelope); `time` is a SECOND, independent noise
// axis, so advancing it morphs the bolt in place instead of sliding the shape sideways.
// `waveCount` = base wiggle count (finer detail comes from the octaves and the kink layer).

struct LightningBoltParams {
    Vector3f   start{ Vector3f::ZERO };
    Vector3f   end{ Vector3f::ZERO };
    int32_t    segments{ 8 };
    int32_t    waveCount{ 1 };
    float      startWidth{ 0.1f };
    float      endWidth{ 0.0f };
    float      amplitude{ 1.0f };
    uint32_t   seed{ 0 };
    float      time{ 0.0f };
    // The noise time the STRUCTURE is decided at - the spawn phase, which does not move while the bolt
    // animates. The swing plane is derived at this phase (see Build), so it stays put instead of
    // rotating along with the writhing.
    float      basePhase{ 0.0f };
    eSwingMode swingMode{ smHorizontal };
    Vector3f   planeNormal{ Vector3f::ZERO };
    float      tailFraction{ 0.0f };
    LightningFbmParams fbm;              // the bundle's noise properties, see LightningFbmParams
};

// -------------------------------------------------------------------------------------------------

class LightningBolt {
public:
    AutoArray<LightningNode> m_nodes;
    // Nodes that are actually drawn. Everything past it is the free tail (tailFraction): it is built so
    // the sin window does NOT close at the visible end, which leaves the tip displaced and dancing
    // instead of pinned. 0 = whole polyline visible.
    int32_t                  m_visibleNodes{ 0 };

    void Build(const LightningBoltParams& params);

    void Clear(void);

    inline int32_t VisibleNodes(void) const {
        int32_t n = m_nodes.Length();
        return ((m_visibleNodes > 0) and (m_visibleNodes < n)) ? m_visibleNodes : n;
    }
};

// -------------------------------------------------------------------------------------------------

enum eLightningType { ltStrike, ltArc };

class BaseLightning {
public:
    eLightningType           m_type;
    Vector3f                 m_start{ Vector3f::ZERO };
    Vector3f                 m_end{ Vector3f::ZERO };
    float                    m_startWidth{ 0.05f };
    float                    m_endWidth{ 0.0f };
    float                    m_amplitudeFactor{ 0.1f };   // amplitude as a fraction of the current |end-start|
    float                    m_waveRatio{ 3.0f };         // wavelength / amplitude -> base jaggedness (coupled to amplitude)
    float                    m_coreWidth{ 0.5f };         // white-core band fraction; per-bolt, handed to the shader via the segment buffer
    Vector3f                 m_color{ Vector3f(1.0f, 1.0f, 1.0f) };   // per-bolt halo tint, likewise through the segment buffer
    eSwingMode               m_swingMode{ smHorizontal }; // see eSwingMode
    Vector3f                 m_planeNormal{ Vector3f::ZERO };
    bool                     m_useElevationCap{ true };
    // Not owned: this points at the LightningFbmParams of the system this lightning belongs to, which
    // outlives it (the system owns its lightnings). nullptr only for a lightning built outside a
    // system - Fbm () then answers with the neutral defaults.
    const LightningFbmParams* m_fbm{ nullptr };

    // The system hands its own instance over when it takes the lightning in (AddStrike / AddArc).
    inline void SetFbm(const LightningFbmParams* fbm) { m_fbm = fbm; }

    inline const LightningFbmParams& Fbm(void) const {
        static const LightningFbmParams defaults;
        return m_fbm ? *m_fbm : defaults;
    }

    float                    m_tailFraction{ 0.0f };
    int32_t                  m_segments{ 8 };             // fixed intermediate-point count (from the base length)
    int32_t                  m_waveCount{ 1 };            // fixed wiggle count (from the base length)
    uint32_t                 m_seed{ 0 };
    float                    m_animSpeed{ 0.0f };         // animation speed; 0 = static (built once). Arc + animated strike both use it.
    float                    m_timeOffset{ 0.0f };        // random start on the noise time axis so different bolts don't look identical
    // Path rebuild rate. The noise time axis is continuous (quintic fade), so two builds a few tens of ms
    // apart already sit close together -- there is no need to interpolate node positions between them,
    // only to not rebuild more often than necessary. 0 = rebuild on every Update.
    float                    m_regenInterval{ 33.0f };
    int64_t                  m_lastGenerated{ 0 };
    // The brightness that was written into the renderer's segment buffer the last time it was built. The
    // renderer only rebuilds when something actually changed, and a static (non-animated) lightning changes
    // nothing but its fade -- so this is what tells the renderer that its buffer went stale. One value, one
    // writer (the renderer), no second bookkeeping.
    float                    m_lastFade{ -1.0f };
    AutoArray<LightningBolt> m_bolts;

    BaseLightning(eLightningType type) : m_type(type) { }

    virtual ~BaseLightning() = default;

    // Move both endpoints (whole buschel follows on the next Generate). No-op geometry change until then.
    inline void SetEndpoints(const Vector3f& start, const Vector3f& end) noexcept {
        m_start = start;
        m_end = end;
    }

    // Follow a moved endpoint on the already-built geometry. Default just re-sets the endpoints (the arc
    // rebuilds every frame anyway); the strike overrides to re-anchor only its end segment (start ignored)
    // so the branches stay put while the tip tracks a slightly swinging target.
    virtual void UpdateEndpoints(const Vector3f& start, const Vector3f& end) { SetEndpoints(start, end); }

    virtual void Generate(int64_t now) = 0;               // (re)build the bolt(s) for the current params/time

    // Rebuild if the regeneration interval has elapsed. Returns true when the geometry actually changed,
    // so the renderer knows whether its segment buffer is still valid.
    bool Regenerate(int64_t now);

    virtual bool IsAlive(int64_t /*now*/) const { return true; }

    virtual float Fade(int64_t /*now*/) const { return 1.0f; } // brightness 0..1 passed to the shader

    // AFTERGLOW: true once the ttl fade window has begun -> the handler drops the white core (full-res
    // core pass) and leaves only the fading halo (see LightningHandler::BuildSegments).
    virtual bool IsFading(int64_t /*now*/) const { return false; }

    virtual bool IsAnimated(void) const { return false; }  // arcs rebuild over time; strikes do not

protected:
    // Copy everything that is common to both kinds out of the creation params.
    void SetupCommon(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params);

    // segments + waveCount from the current endpoint distance (fixed at setup -> self-similar scaling).
    void ComputeCounts(void);

    inline float CurrentLength(void) const { return (m_end - m_start).Length(); }
};

// -------------------------------------------------------------------------------------------------

class LightningStrike : public BaseLightning {
public:
    int64_t m_spawnTime{ 0 };
    float   m_lifetime{ 1.0f };
    float   m_fadeStart{ 150.0f };   // ms before the end of lifetime at which the afterglow fade begins
    int32_t m_branchDepth{ 2 };   // recursion depth: 0 = trunk only, 1 = trunk has branches, 2 = branches have branches, ...
    float   m_branchChance{ 1.0f };   // probability [0,1] that a sub-branch forks at each eligible node
    int32_t m_maxBranchTestSkips{ 0 };   // after a fork, skip Random::Int(this) nodes before testing again (0 = never skip)

    LightningStrike() : BaseLightning(ltStrike) { }

    void Setup(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params, int64_t spawnTime);

    void Generate(int64_t now) override;                  // main bolt + branches, built once

    bool IsAlive(int64_t now) const override;

    float Fade(int64_t now) const override;               // full until fadeStart ms before the end, then linear decay to 0 at lifetime end; modulated by flicker

    bool IsFading(int64_t now) const override;            // AFTERGLOW: inside the fade window -> halo only

    void UpdateEndpoints(const Vector3f& start, const Vector3f& end) override;   // ignore start, re-anchor the main bolt's last node

    bool IsAnimated(void) const override { return m_animSpeed > 0.0f; }   // animSpeed > 0 -> rebuilt over time (wabers); structure stays fixed (seeded)

private:
    // branch structure is a deterministic function of `seed` (stable across frames); only `time` (the noise
    // time axis) advances, so the strike wabers in place without the branches jumping around.
    void AddBolt(const Vector3f& start, const Vector3f& end, float startWidth, float endWidth, int32_t depth, uint32_t seed, float time);
};

// -------------------------------------------------------------------------------------------------

class LightningArc : public BaseLightning {
public:
    int32_t m_boltCount{ 1 };

    LightningArc() : BaseLightning(ltArc) { }

    void Setup(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params);

    void Generate(int64_t now) override;                  // rebuild the n-bolt bundle at the current time phase

    bool IsAnimated(void) const override { return true; }
};

// =================================================================================================
// What the renderer hands to the GPU. Kept here, next to the geometry, so every application that draws
// these bolts uses ONE layout -- the shaders declare the same fields as flat floats (std430 would inflate
// vec3 to 16 bytes otherwise).
//
// One ribbon segment: prev/next are the neighbouring polyline nodes, from which the vertex shader builds
// a miter join so the strip stays gap- and overlap-free at the joints. coreWidth and color are per-bolt
// values that ride along per segment (a draw covers bolts of different appearance).

struct LightningSegment {
    Vector3f p0;    float w0;
    Vector3f p1;    float w1;
    Vector3f prev;  float fade;
    Vector3f next;  float coreWidth;
    Vector3f color; float pad;        // halo tint of this bolt; multiplies the shader's haloColor
};

static_assert(sizeof(LightningSegment) == 80, "LightningSegment must stay 80 bytes (GPU StructuredBuffer layout)");

// One impact flare: a view-aligned billboard drawn at a bolt endpoint, nudged toward the viewer onto the
// front face and depth-tested against the scene.

struct LightningFlare {
    Vector3f position;  float fade;
    float    width;     Vector3f color;   // width = the bolt's half-width at this endpoint -> the flare scales with the tip
};

static_assert(sizeof(LightningFlare) == 32, "LightningFlare must stay 32 bytes (GPU StructuredBuffer layout)");

// =================================================================================================
