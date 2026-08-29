#include <cmath>
#include "lightningemitter.h"
#include "lightningsystem.h"
#include "random.hpp"

// =================================================================================================

namespace {
    constexpr float TwoPi = 6.28318530718f;
    constexpr float DegToRad = 0.01745329252f;

    // value * (1 +/- jitter)
    float Jitter(float value, float jitter) {
        if (jitter <= 0.0f)
            return value;
        if (jitter > 1.0f)
            jitter = 1.0f;
        return value * (1.0f - jitter + Random::Float(2.0f * jitter));
    }

    // Uniform random direction inside a cone of half-angle coneAngle (deg) around axis. 180 deg = the whole
    // sphere, and then the draw is a proper uniform sphere sample (cos theta uniform in [-1, 1]).
    Vector3f RandomDirection(const Vector3f& axis, float coneAngle) {
        Vector3f a = axis;
        float l = a.Length();
        if (l > 1e-4f)
            a = a * (1.0f / l);
        else
            a = Vector3f(0.0f, 1.0f, 0.0f);
        if (coneAngle > 180.0f)
            coneAngle = 180.0f;
        else if (coneAngle < 0.0f)
            coneAngle = 0.0f;
        float cosCone = std::cos(coneAngle * DegToRad);
        float z = 1.0f - Random::Float(1.0f) * (1.0f - cosCone);   // cos(theta) uniform in [cosCone, 1]
        float r = 1.0f - z * z;
        r = (r > 0.0f) ? std::sqrt(r) : 0.0f;
        float phi = Random::Float(TwoPi);
        Vector3f ref = (std::fabs(a.y) > 0.9f) ? Vector3f(1.0f, 0.0f, 0.0f) : Vector3f(0.0f, 1.0f, 0.0f);
        Vector3f u = ref.Cross(a);
        float ul = u.Length();
        u = (ul > 1e-4f) ? u * (1.0f / ul) : Vector3f(1.0f, 0.0f, 0.0f);
        Vector3f v = a.Cross(u);
        return a * z + u * (r * std::cos(phi)) + v * (r * std::sin(phi));
    }
}

// =================================================================================================

void LightningEmitter::Setup(const Vector3f& start, const Vector3f& end, const LightningCreationParams& params, const LightningEmitterParams& emitterParams, int64_t now) {
    m_params = params;
    m_emitterParams = emitterParams;
    m_start = start;
    m_end = end;
    m_burning = false;
    m_ignited = false;
    m_burnUntil = now;
    m_nextIgnition = now;   // fire on the first Update
}


void LightningEmitter::DrawEndpoints(Vector3f& start, Vector3f& end) const {
    start = m_start;
    if (m_emitterParams.endpointMode == epRandomDirection) {
        Vector3f reference = m_end - m_start;
        if (reference.Length() < 1e-4f)
            reference = Vector3f(0.0f, 1.0f, 0.0f);
        Vector3f dir = RandomDirection(reference, m_emitterParams.coneAngle);
        end = start + dir * Jitter(m_emitterParams.radius, m_emitterParams.radiusJitter);
    }
    else
        end = m_end;
    // push both ends outward along the bolt direction, so a bundle does not all start in one point
    if (m_emitterParams.startOffset > 0.0f) {
        Vector3f delta = end - start;
        float length = delta.Length();
        if (length > 1e-4f) {
            float offset = m_emitterParams.startOffset * 0.5f + Random::Float(m_emitterParams.startOffset * 0.5f);
            Vector3f step = delta * (offset / length);
            start += step;
            end += step;
        }
    }
}


void LightningEmitter::Ignite(int64_t now, LightningSystem& system) {
    system.Clear();
    int32_t count = (m_emitterParams.count < 1) ? 1 : m_emitterParams.count;
    for (int32_t i = 0; i < count; i++) {
        Vector3f start, end;
        DrawEndpoints(start, end);
        if (m_emitterParams.kind == lkArc)
            system.AddArc(start, end, m_params);
        else
            system.AddStrike(start, end, m_params, now);
    }
    float burnTime = Jitter(m_params.lifetime, m_emitterParams.timeJitter);
    if (burnTime < 0.0f)
        burnTime = 0.0f;
    m_burnUntil = now + int64_t(burnTime * 1000.0f);
    m_nextIgnition = m_burnUntil + int64_t(Jitter(m_emitterParams.offTime, m_emitterParams.timeJitter) * 1000.0f);
    m_burning = true;
    m_ignited = true;
}


bool LightningEmitter::Update(int64_t now, LightningSystem& system) {
    bool changed = false;
    // lmPersistent never ends on its own (the classic arc: it burns until the application destroys it),
    // so neither the burn timer nor a re-ignition applies to it.
    if (m_emitterParams.mode == lmPersistent) {
        if (not m_ignited) {
            Ignite(now, system);
            m_burning = true;
            m_burnUntil = 0;
            m_nextIgnition = 0;
            changed = true;
        }
        return changed;
    }

    if (not m_ignited) {
        if (now >= m_nextIgnition) {
            Ignite(now, system);
            changed = true;
        }
        return changed;
    }

    if (m_burning and (now >= m_burnUntil)) {
        // A strike has faded out by itself at this point (its lifetime IS the burn time); an arc has not,
        // so extinguishing is done here for both -- one rule, no special case.
        if (not system.IsEmpty()) {
            system.Clear();
            changed = true;
        }
        m_burning = false;
    }
    else if (m_burning) {
        // strikes die individually (their own ttl) -- take them out so a partly burnt-down bundle does not
        // keep feeding dead lightnings to the renderer
        int32_t before = system.m_lightnings.Length();
        system.RemoveDead(now);
        changed = (system.m_lightnings.Length() != before);
    }

    if ((not m_burning) and (m_emitterParams.mode == lmRepeating) and (now >= m_nextIgnition)) {
        Ignite(now, system);
        changed = true;
    }
    return changed;
}

// =================================================================================================
