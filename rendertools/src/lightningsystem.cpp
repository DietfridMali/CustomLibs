#include "lightningsystem.h"

// =================================================================================================

LightningSystem::~LightningSystem() {
    Clear();
    delete m_emitter;
    m_emitter = nullptr;
}


void LightningSystem::Clear(void) {
    for (int32_t i = 0; i < m_lightnings.Length(); i++)
        delete m_lightnings[i];
    m_lightnings.Clear();
}


LightningStrike* LightningSystem::AddStrike(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params, int64_t now) {
    LightningStrike* strike = new LightningStrike();
    strike->Setup(start, end, params, now);
    m_lightnings.Append(strike);
    return strike;
}


LightningArc* LightningSystem::AddArc(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params) {
    LightningArc* arc = new LightningArc();
    arc->Setup(start, end, params);
    m_lightnings.Append(arc);
    return arc;
}


LightningEmitter* LightningSystem::SetEmitter(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params, const LightningEmitterParams& emitterParams, int64_t now) {
    delete m_emitter;
    m_emitter = new LightningEmitter();
    m_emitter->Setup(start, end, params, emitterParams, now);
    m_ttl = emitterParams.ttl;   // the emitter's safety net IS the system's ttl - one source of truth
    m_spawnTime = now;
    return m_emitter;
}


void LightningSystem::SetEndpoints(const Vector3f& start, const Vector3f& end) {
    if (m_emitter != nullptr)
        m_emitter->SetEndpoints(start, end);   // where the NEXT ignition goes
    for (int32_t i = 0; i < m_lightnings.Length(); i++)
        m_lightnings[i]->SetEndpoints(start, end);
}


void LightningSystem::UpdateEndpoints(const Vector3f& start, const Vector3f& end) {
    if (m_emitter != nullptr)
        m_emitter->SetEndpoints(start, end);
    for (int32_t i = 0; i < m_lightnings.Length(); i++)
        m_lightnings[i]->UpdateEndpoints(start, end);
}


void LightningSystem::RemoveDead(int64_t now) {
    int32_t count = m_lightnings.Length();
    int32_t survivors = 0;
    for (int32_t i = 0; i < count; i++) {
        if (m_lightnings[i]->IsAlive(now))
            m_lightnings[survivors++] = m_lightnings[i];
        else
            delete m_lightnings[i];
    }
    if (survivors < count)
        m_lightnings.Resize(survivors);   // AutoArray is shrinkable by default
}


bool LightningSystem::Update(int64_t now) {
    bool changed = false;
    if (m_emitter != nullptr)
        changed = m_emitter->Update(now, *this);
    // The path is only rebuilt when the regeneration interval has elapsed -- the noise time axis is
    // continuous, so a bolt that holds its shape for a few frames does not jump, it simply waits.
    for (int32_t i = 0; i < m_lightnings.Length(); i++) {
        if (m_lightnings[i]->Regenerate(now))
            changed = true;
    }
    return changed;
}


bool LightningSystem::IsExpired(int64_t now) const {
    return (m_ttl > 0) and (now - m_spawnTime >= m_ttl);
}

// =================================================================================================
