#pragma once

#include <cstdint>
#include "lightningbolt.h"
#include "lightningemitter.h"
#include "vector.hpp"
#include "array.hpp"

// =================================================================================================
// A group of lightnings (strikes / arcs -- the "Buschel") that share a lifecycle and move together via
// SetEndpoints. Owned by the LightningHandler by id. ttl <= 0 -> permanent (until Destroy(id));
// ttl > 0 -> the handler auto-reaps it once the ttl elapses. Owns its lightnings by raw pointer
// (single owner, deleted in the dtor) -- non-copyable to keep that ownership safe in containers.
//
// A system may additionally own a LightningEmitter, which re-ignites its content over time (blinking
// discharges, explosion rods, anything that is not a single one-shot). Without one the system is just
// the container it always was.

class LightningSystem {
public:
    AutoArray<BaseLightning*> m_lightnings;   // owned; deleted in the dtor
    LightningEmitter*         m_emitter{ nullptr };   // owned; nullptr = no time control
    int64_t m_spawnTime{ 0 };
    int64_t m_ttl{ 0 };                       // ms; <= 0 = permanent

    LightningSystem() = default;

    LightningSystem(int64_t ttlMs, int64_t now) : m_spawnTime(now), m_ttl(ttlMs) { }

    LightningSystem(const LightningSystem&) = delete;
    LightningSystem& operator=(const LightningSystem&) = delete;

    ~LightningSystem();

    LightningStrike* AddStrike(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params, int64_t now);

    LightningArc* AddArc(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params);

    // Attach a time control. The system takes ownership; its ttl becomes the emitter's safety net.
    LightningEmitter* SetEmitter(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params, const LightningEmitterParams& emitterParams, int64_t now);

    void SetEndpoints(const Vector3f& start, const Vector3f& end);

    void UpdateEndpoints(const Vector3f& start, const Vector3f& end);   // re-anchor already-built geometry to a moved endpoint

    // Emitter first (it may ignite or extinguish), then rebuild the animated lightnings whose regeneration
    // interval has elapsed. Returns true if any geometry changed this frame -> the renderer's segment
    // buffer is stale. Returns false when nothing moved, and then the buffer can simply be reused.
    bool Update(int64_t now);

    void RemoveDead(int64_t now);             // drop the lightnings whose lifetime has run out

    void Clear(void);                         // delete every lightning (the emitter stays)

    inline bool IsEmpty(void) const { return m_lightnings.Length() < 1; }

    bool IsExpired(int64_t now) const;
};

// =================================================================================================
