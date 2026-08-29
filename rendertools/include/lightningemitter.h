#pragma once

#include <cstdint>
#include "lightningbolt.h"
#include "vector.hpp"

class LightningSystem;

// =================================================================================================
// The time control of a lightning system: it ignites the discharges, lets them burn for their lifetime,
// pauses, and ignites again -- until the application destroys it. This is what a blinking discharge
// (damaged robot, sparking level machinery, a player caught in a fuel centre) is made of, and it is also
// where "a bundle of rods flying off in random directions" lives: the endpoint of each single lightning
// is drawn fresh at every ignition, so an explosion or a teleport effect is one emitter, not n bolts the
// application has to place itself.
//
// Modes:
//   lmOneShot    - ignite once, then done (the system is reaped by its ttl).
//   lmRepeating  - burn / pause / burn ... forever, until Destroy () or the optional safety-net ttl.
//   lmPersistent - ignite once and keep burning (the classic arc: omega beam, force field).
//
// Endpoint policy:
//   epFixed           - both endpoints come from the application (SetEndpoints), unchanged per ignition.
//   epRandomDirection - the start is the application's, the end is drawn per lightning: a random direction
//                       within coneAngle of the reference direction (end - start), times radius. coneAngle
//                       180 = the full sphere -> ends land anywhere on a sphere of that radius.

enum eLightningKind { lkStrike, lkArc };

enum eLightningMode { lmOneShot, lmRepeating, lmPersistent };

enum eEndpointMode { epFixed, epRandomDirection };

// -------------------------------------------------------------------------------------------------

struct LightningEmitterParams {
    eLightningKind kind{ lkStrike };
    eLightningMode mode{ lmRepeating };
    int32_t        count{ 1 };        // lightnings created per ignition (explosion rods: many)
    float          offTime{ 0.0f };   // seconds of darkness between one discharge and the next
    float          timeJitter{ 0.25f };  // +/- fraction applied to burn time and off time, so emitters don't pulse in lockstep
    eEndpointMode  endpointMode{ epFixed };
    float          radius{ 0.0f };    // epRandomDirection: distance of the drawn end from the start
    float          radiusJitter{ 0.25f };  // +/- fraction on the radius
    float          coneAngle{ 180.0f };  // epRandomDirection: max angle (deg) off the reference direction; 180 = full sphere
    float          startOffset{ 0.0f };  // push both endpoints out along the bolt direction by [offset/2, offset] (keeps bolts off an object's centre)
    int64_t        ttl{ 0 };          // safety net in ms: 0 = lives until Destroy (); > 0 = the system is reaped anyway
};

// -------------------------------------------------------------------------------------------------

class LightningEmitter {
public:
    LightningCreationParams m_params;                 // handed to every lightning it ignites
    LightningEmitterParams  m_emitterParams;
    Vector3f                m_start{ Vector3f::ZERO };
    Vector3f                m_end{ Vector3f::ZERO };
    int64_t                 m_burnUntil{ 0 };         // end of the current discharge
    int64_t                 m_nextIgnition{ 0 };      // start of the next one
    bool                    m_burning{ false };
    bool                    m_ignited{ false };       // has fired at least once (lmOneShot / lmPersistent)

    LightningEmitter() = default;

    void Setup(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params, const LightningEmitterParams& emitterParams, int64_t now);

    // Moves the source. The running discharge is NOT re-aimed here -- the system does that for its
    // lightnings; this only decides where the NEXT ignition happens.
    inline void SetEndpoints(const Vector3f& start, const Vector3f& end) noexcept {
        m_start = start;
        m_end = end;
    }

    // Ignite / extinguish as the clock demands. Returns true if the system's content changed (something
    // was created or removed), so the renderer knows its segment buffer is stale.
    bool Update(int64_t now, LightningSystem& system);

private:
    void Ignite(int64_t now, LightningSystem& system);

    // One pair of endpoints for a single lightning of this ignition (per-lightning random draw).
    void DrawEndpoints(Vector3f& start, Vector3f& end) const;
};

// =================================================================================================
