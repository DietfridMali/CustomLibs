#include "linesegment.h"

// =================================================================================================

// Project p on line, return projected point on line in f and distance p,f as function result
float LineSegment::Project(const Vector3f& p, Vector3f& f)
{
	float l2 = properties.length * properties.length;

    if (l2 < m_tolerance) {// line too short, projection may fail due to numerical limitations
        f = p0;
        return 0.0f;
    }
    float t = (p - p0).Dot(properties.velocity) / l2; // project (p - p0) on (p0,p1)
	f = p0 + Velocity() * t; // determine foot point on line
	return (p - f).Length();
}


float LineSegment::Distance(const Vector3f& p)
{
    float l2 = properties.length * properties.length;

    if (l2 < m_tolerance) // line too short, projection may fail due to numerical limitations
        return 0.0f;
    Vector3f u = p;
    u -= p0;
    float t = u.Dot(properties.velocity) / l2; // project (p - p0) on (p0,p1)
    return Length() * t;
}


// Findet den/alle Punkt(e) auf der Geraden durch das Segment (*this),
// der/die im Abstand 'radius' zu 'refPoint' liegen.
// Gibt true zurück, falls mindestens eine Lösung im gewünschten t-Bereich existiert.
// Optional: Gibt den Punkt mit dem kleinsten |t| zurück (d. h. den, der p0 am nächsten liegt).
int LineSegment::ComputeNearestPointsAt(const Vector3f& p, float radius, const Conversions::FloatInterval& limits)
{
    // Unterschiedsvektor zum Referenzpunkt
    Vector3f delta = p0 - p;

    float A = Velocity().Dot(Velocity());
    float B = 2.0f * Velocity().Dot(delta);
    float C = delta.Dot(delta) - radius * radius;

    float D = B * B - 4.0f * A * C;
    if (D < 0.0f)
        return 0; // Keine reelle Lösung (zu weit entfernt)

    float sqrtD = sqrtf(D);
    float tBest = limits.max;
    float minAbsT = std::numeric_limits<float>::max();

    solutions = 0;
    for (int sign : {-1, 1}) {
        float t = (-B + sign * sqrtD) / (2.0f * A);

        // Hier können Sie die zulässigen t-Grenzen wählen:
        // t <= 1.0f bedeutet: maximal bis einschließlich p1,
        // t beliebig negativ erlaubt Punkte auch "hinter" p0.
        if ((t >= limits.min) and (t <= limits.max)) { // m_tolerance ggf. Member/Const
            offsets[solutions++] = t;
        }
    }
    return solutions;
}


float LineSegment::ComputeNearestPoints(LineSegment& other, LineSegment& nearestPoints)
{
    nearestPoints.solutions = 0;

    const Vector3f d1 = this->Velocity();          // *this: p0 -> p1 (Gerade)
    const Vector3f d2 = other.Velocity();          // other: q0 -> q1 (Gerade)
    const Vector3f r = this->p0 - other.p0;

    const float a = d1.Dot(d1);                    // d1·d1
    const float e = d2.Dot(d2);                    // d2·d2

    // Degenerationen
    if (a <= m_tolerance and e <= m_tolerance) {
        // Punkt–Punkt: nur ein nächster Punkt auf *this* (p0 selbst)
        nearestPoints.endPoints[0] = this->p0;
        nearestPoints.offsets[0] = 0.0f;
        nearestPoints.solutions = 1;
        return (this->p0 - other.p0).Length();
    }

    if (a <= m_tolerance) {
        // *this ist Punkt -> einziger nächster Punkt auf *this* ist p0
        nearestPoints.endPoints[0] = this->p0;
        nearestPoints.offsets[0] = 0.0f;
        nearestPoints.solutions = 1;

        // Distanz Punkt–Gerade(other)
        if (e <= m_tolerance) return (this->p0 - other.p0).Length();
        Vector3f cx = (this->p0 - other.p0).Cross(d2);
        return cx.Length() / std::sqrt(e);
    }

    if (e <= m_tolerance) {
        // other ist Punkt -> projiziere other.p0 auf *this*-Gerade
        float s = ScalarProjection(this->p0, other.p0, d1);
        Vector3f p = this->p0 + d1 * s;

        nearestPoints.endPoints[0] = p;
        nearestPoints.offsets[0] = s;
        nearestPoints.solutions = 1;

        return (p - other.p0).Length();
    }

    // Allgemeiner Fall
    const float b = d1.Dot(d2);                    // d1·d2
    const float c = d1.Dot(r);                     // d1·(p0 - q0)
    const float f = d2.Dot(r);                     // d2·(p0 - q0)
    const float denom = a * e - b * b;             // == |d1×d2|^2

    if (std::fabs(denom) <= m_tolerance) {
        // Quasi-parallel: unendlich viele nächste Punkte -> nimm Projektionen der Endpunkte von 'other' auf *this*
        float s0 = ScalarProjection(this->p0, other.p0, d1);
        float s1 = ScalarProjection(this->p0, other.p1, d1);

        nearestPoints.endPoints[0] = this->p0 + d1 * s0;
        nearestPoints.endPoints[1] = this->p0 + d1 * s1;
        nearestPoints.offsets[0] = s0;
        nearestPoints.offsets[1] = s1;
        nearestPoints.solutions = 2;

        // konstante Linien-Distanz (zu other.p0)
        Vector3f cx = (other.p0 - this->p0).Cross(d1);
        return cx.Length() / std::sqrt(a);
    }

    // Schiefe Geraden: genau ein nächster Punkt auf *this*
    float s = (b * f - c * e) / denom;            // Parameter auf *this*
    float t = (a * f - b * c) / denom;            // Parameter auf other (nur für Distanz)
    Vector3f pClosest = this->p0 + d1 * s;
    Vector3f qClosest = other.p0 + d2 * t;

    nearestPoints.endPoints[0] = pClosest;
    nearestPoints.offsets[0] = s;
    nearestPoints.solutions = 1;

    return (pClosest - qClosest).Length();
}


int LineSegment::ComputeCapsuleIntersection(LineSegment& other, LineSegment& collisionPoints, float radius, const Conversions::FloatInterval& limits)
{
    collisionPoints.solutions = 0;

    const Vector3f d = this->Velocity();     // P: p0 -> p1
    const Vector3f e = other.Velocity();     // G: g0 -> g1
    const float    dd = d.Dot(d);
    const float    ee = e.Dot(e);
    const float    r2 = radius * radius;

    // on-the-fly: behalte den besten (größten) gültigen t (mit t <= 1, t darf < 0)
    auto keepIfBetter = [&](float t) {
        if (not limits.Contains(t)) return;
        if (t > 1.0f + m_tolerance) return;
        Vector3f w = this->p0 + d * t;
        if (collisionPoints.solutions == 0 or t > collisionPoints.offsets[0]) {
            collisionPoints.endPoints[0] = w;
            collisionPoints.offsets[0] = t;
            collisionPoints.solutions = 1;
        }
        };

    // Degeneriert: P ist Punkt -> nur t=0 möglich
    if (dd <= m_tolerance) {
        // Abstand p0 zu Segment G (Projektion auf G, dann clamp)
        float sLin = (ee > m_tolerance) ? ScalarProjection(other.p0, this->p0, e) : 0.0f;
        float sSeg = (ee > m_tolerance) ? std::clamp(sLin, 0.0f, 1.0f) : 0.0f;
        Vector3f q = other.p0 + e * sSeg;
        float dist = (this->p0 - q).Length();
        if (std::fabs(dist - radius) <= m_tolerance and limits.Contains(0.0f)) {
            collisionPoints.endPoints[0] = this->p0;
            collisionPoints.offsets[0] = 0.0f;
            collisionPoints.solutions = 1;
        }
        return collisionPoints.solutions; // 0 oder 1
    }

    // ==========================================================
    // 1) Mantel: erst Nächstpunkt(e) P<->G über ComputeNearestPoints holen
    //    - nicht parallel: 1 Punkt p* auf P (t*), Linienabstand δ
    //      -> Kandidaten t = t* ± Δt, nur wenn s(t) ∈ [0,1]
    //    - parallel: 2 Parameter (Projektionen von other-Endpunkten auf P)
    //      -> falls δ ≈ r, alle t im Intervall sind gültig -> nimm größtes zulässiges t
    {
        LineSegment nearest;
        float delta = this->ComputeNearestPoints(other, nearest); // füllt offsets/punkte auf *this*
        if (ee > m_tolerance) {
            if (nearest.solutions == 1) {
                // nicht parallel (oder other ist Punkt -> Kappen regeln)
                const float tStar = nearest.offsets[0];
                const Vector3f pStar = nearest.endPoints[0];

                float sStar = ScalarProjection(other.p0, pStar, e); // Projektion von p* auf G-Linie
                if (sStar >= -m_tolerance and sStar <= 1.0f + m_tolerance and delta <= radius + m_tolerance) {
                    Vector3f cx = d.Cross(e);
                    float c2 = cx.Dot(cx);            // |d×e|^2
                    if (c2 > m_tolerance) {
                        float inside = r2 - delta * delta;
                        float dt = (inside > 0.0f) ? std::sqrt(inside * ee / c2) : 0.0f;

                        // s(t) ist linear in t: s(t) = s* + beta (t - t*)
                        float beta = d.Dot(e) / ee;
                        auto tryCand = [&](float tCand) {
                            float sCand = sStar + beta * (tCand - tStar);
                            if (sCand >= -m_tolerance and sCand <= 1.0f + m_tolerance)
                                keepIfBetter(tCand);
                            };
                        tryCand(tStar - dt);
                        if (dt > m_tolerance) tryCand(tStar + dt);
                    }
                }
            }
            else if (nearest.solutions == 2) {
                // parallel: konstante Linien-Distanz delta
                if (std::fabs(delta - radius) <= m_tolerance) {
                    float tMin = std::min(nearest.offsets[0], nearest.offsets[1]);
                    float tMax = std::max(nearest.offsets[0], nearest.offsets[1]);
                    // größtes t in Schnittmenge [tMin, tMax] ∩ [limits.min, min(limits.max, 1)]
                    float upper = std::min(1.0f, limits.max);
                    float lower = limits.min;
                    float hi = std::min(tMax, upper);
                    float lo = std::max(tMin, lower);
                    if (hi >= lo - m_tolerance) keepIfBetter(hi);
                }
            }
        }
    }

    // ==========================================================
    // 2) Kappen: Endpunkte g0,g1 als Sphären prüfen (Project/Pythagoras),
    //    pro Kappe sofort den größten zulässigen t ≤ 1 behalten
    auto capOnP = [&](const Vector3f& c) {
        Vector3f w = this->p0 - c;
        Vector3f wxd = w.Cross(d);
        float delta2 = wxd.Dot(wxd) / dd;       // Abstand^2 c -> P-Linie
        if (delta2 > r2 + m_tolerance) return;

        float tFoot = ScalarProjection(this->p0, c, d);
        float dt = 0.0f;
        if (r2 > delta2) dt = std::sqrt((r2 - delta2) / dd);

        float tA = tFoot - dt;
        float tB = tFoot + dt;

        if (tA <= 1.0f + m_tolerance) keepIfBetter(tA);
        if (std::fabs(tB - tA) > m_tolerance and tB <= 1.0f + m_tolerance) keepIfBetter(tB);
        };

    if (ee > m_tolerance) {
        capOnP(other.p0);
        capOnP(other.p1);
    }
    else {
        // other degeneriert (Punkt) -> nur eine "Kappe"
        capOnP(other.p0);
    }

    return collisionPoints.solutions; // 1 bei Erfolg, sonst 0
}

#endif

// =================================================================================================
