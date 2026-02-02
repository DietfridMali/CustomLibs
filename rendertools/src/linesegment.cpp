#include "linesegment.h"

// =================================================================================================

// Project p on line, return projected point on line in f and distance p,f as function result
float LineSegment::Project(const Vector3f& p, Vector3f& f)
noexcept
{
    float l2 = properties.length * properties.length;

    if (l2 < m_tolerance) {// line too short, projection may fail due to numerical limitations
        f = pts.p0;
        return 0.0f;
    }
    float t = (p - pts.p0).Dot(properties.velocity) / l2; // project (p - p0) on (p0,p1)
    f = pts.p0 + Velocity() * t; // determine foot point on line
    return (p - f).Length();
}

float LineSegment::Distance(const Vector3f& p)
noexcept
{
    float l2 = properties.length * properties.length;

    if (l2 < m_tolerance) // line too short, projection may fail due to numerical limitations
        return 0.0f;
    Vector3f u = p;
    u -= pts.p0;
    float t = u.Dot(properties.velocity) / l2; // project (p - p0) on (p0,p1)
    return Length() * t;
}

// Findet den/alle Punkt(e) auf der Geraden durch das Segment (*this),
// der/die im Abstand 'radius' zu 'refPoint' liegen.
// Gibt true zurück, falls mindestens eine Lösung im gewünschten t-Bereich existiert.
// Optional: Gibt den Punkt mit dem kleinsten |t| zurück (d. h. den, der p0 am nächsten liegt).
int LineSegment::ComputeNearestPointsAt(const Vector3f& p, float radius, const Conversions::FloatInterval& limits)
noexcept
{
    // Unterschiedsvektor zum Referenzpunkt
    Vector3f delta = pts.p0 - p;

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
noexcept
{
    nearestPoints.solutions = 0;

    const Vector3f d1 = this->Velocity();          // *this: p0 -> p1 (Gerade)
    const Vector3f d2 = other.Velocity();          // other: q0 -> q1 (Gerade)
    const Vector3f r = pts.p0 - other.pts.p0;

    const float a = d1.Dot(d1);                    // d1·d1
    const float e = d2.Dot(d2);                    // d2·d2

    // Degenerationen
    if (a <= m_tolerance and e <= m_tolerance) {
        // Punkt–Punkt: nur ein nächster Punkt auf *this* (p0 selbst)
        nearestPoints.vec[0] = pts.p0;
        nearestPoints.offsets[0] = 0.0f;
        nearestPoints.solutions = 1;
        return (pts.p0 - other.pts.p0).Length();
    }

    if (a <= m_tolerance) {
        // *this ist Punkt -> einziger nächster Punkt auf *this* ist p0
        nearestPoints.vec[0] = pts.p0;
        nearestPoints.offsets[0] = 0.0f;
        nearestPoints.solutions = 1;

        // Distanz Punkt–Gerade(other)
        if (e <= m_tolerance) return (pts.p0 - other.pts.p0).Length();
        Vector3f cx = (pts.p0 - other.pts.p0).Cross(d2);
        return cx.Length() / std::sqrt(e);
    }

    if (e <= m_tolerance) {
        // other ist Punkt -> projiziere other.p0 auf *this*-Gerade
        float s = ScalarProjection(pts.p0, other.pts.p0, d1);
        Vector3f p = pts.p0 + d1 * s;

        nearestPoints.vec[0] = p;
        nearestPoints.offsets[0] = s;
        nearestPoints.solutions = 1;

        return (p - other.pts.p0).Length();
    }

    // Allgemeiner Fall
    const float b = d1.Dot(d2);                    // d1·d2
    const float c = d1.Dot(r);                     // d1·(p0 - q0)
    const float f = d2.Dot(r);                     // d2·(p0 - q0)
    const float denom = a * e - b * b;             // == |d1×d2|^2

    if (std::fabs(denom) <= m_tolerance) {
        // Quasi-parallel: unendlich viele nächste Punkte -> nimm Projektionen der Endpunkte von 'other' auf *this*
        float s0 = ScalarProjection(pts.p0, other.pts.p0, d1);
        float s1 = ScalarProjection(pts.p0, other.pts.p1, d1);

        nearestPoints.vec[0] = pts.p0 + d1 * s0;
        nearestPoints.vec[1] = pts.p0 + d1 * s1;
        nearestPoints.offsets[0] = s0;
        nearestPoints.offsets[1] = s1;
        nearestPoints.solutions = 2;

        // konstante Linien-Distanz (zu other.p0)
        Vector3f cx = (other.pts.p0 - pts.p0).Cross(d1);
        return cx.Length() / std::sqrt(a);
    }

    // Schiefe Geraden: genau ein nächster Punkt auf *this*
    float s = (b * f - c * e) / denom;            // Parameter auf *this*
    float t = (a * f - b * c) / denom;            
    Vector3f pClosest = pts.p0 + d1 * s;
    Vector3f qClosest = other.pts.p0 + d2 * t;

    nearestPoints.vec[0] = pClosest;
    nearestPoints.offsets[0] = s;
    nearestPoints.solutions = 1;

    return (pClosest - qClosest).Length();
}

// -------------------------------------------------------------------------------------------------

bool LineSegment::CapCheckOnP(const Vector3f& c, const Vector3f& d, float dd, float radius, const Conversions::FloatInterval& limits, float& tSel) const
noexcept
{
    const float r2 = radius * radius;

    // δ_c^2 = ||(p0 - c) × d||^2 / ||d||^2
    Vector3f w = pts.p0; w -= c;
    Vector3f wxd = w.Cross(d);
    float delta2 = wxd.Dot(wxd) / dd;
    if (delta2 > r2 + Conversions::NumericTolerance)
        return false;

    // Fußpunkt + Pythagoras entlang P
    float tFoot = ScalarProjection(pts.p0, c, d);
    float inside = r2 - delta2;
    float dt = (inside > 0.0f) ? std::sqrt(inside / dd) : 0.0f;

    float tA = tFoot - dt;
    float tB = tFoot + dt;

    // größtes t ≤ 1, innerhalb limits
    tSel = -std::numeric_limits<float>::infinity();
    if (limits.Contains(tA) and (tA <= 1.0f + Conversions::NumericTolerance))
        tSel = std::max(tSel, tA);
    if ((std::fabs(tB - tA) > Conversions::NumericTolerance) and limits.Contains(tB) and (tB <= 1.0f + Conversions::NumericTolerance))
        tSel = std::max(tSel, tB);
    return tSel != -std::numeric_limits<float>::infinity();
}

// -------------------------------------------------------------------------------------------------

int LineSegment::ComputeCapsuleIntersection(LineSegment& other, LineSegment& collisionPoints, float radius, const Conversions::FloatInterval& limits)
noexcept
{
    collisionPoints.solutions = 0;

    const Vector3f d = this->Velocity();     // P: p0 -> p1
    const Vector3f e = other.Velocity();     // G: g0 -> g1
    const float    dd = d.Dot(d);
    const float    ee = e.Dot(e);
    const float    tol = m_tolerance;
    const float    r2 = radius * radius;

    if (dd <= tol) return 0;                  // P degeneriert

    // kleine lokale Helfer
    auto acceptS01 = [&](float s) -> bool { return s >= -tol and s <= 1.0f + tol; };
    auto acceptT = [&](float t) -> bool {
        if (not limits.Contains(t)) return false;
        if (t > 1.0f + tol)         return false;     // nur t ≤ 1
        return true;                                   // t kann < 0 sein – Filter später in CoplanarRectangle::SphereIntersection
        };
    auto keepBestT = [&](float t) {
        if (not acceptT(t)) return;
        Vector3f w = pts.p0 + d * t;
        if (collisionPoints.solutions == 0 or t > collisionPoints.offsets[0]) {
            collisionPoints.vec[0] = w;   // Punkt auf *this*
            collisionPoints.offsets[0] = t;   // Parameter t
            collisionPoints.solutions = 1;
        }
        };

    // ---------- 1) Mantel via ComputeNearestPoints ----------
    {
        LineSegment nearest;
        float delta = this->ComputeNearestPoints(other, nearest); // liefert t* auf *this*; bei Parallelität 2 Parameter

        if (ee > tol) {
            if (nearest.solutions == 1) {
                const float    tStar = nearest.offsets[0];
                const Vector3f pStar = nearest.vec[0];

                float sStar = ScalarProjection(other.pts.p0, pStar, e); // Projektion auf G
                if (acceptS01(sStar) and delta <= radius + tol) {
                    Vector3f cx = d.Cross(e);
                    float   c2 = cx.Dot(cx);          // |d×e|^2
                    if (c2 > tol) {
                        float inside = r2 - delta * delta;
                        float dt = (inside > 0.0f) ? std::sqrt(inside * ee / c2) : 0.0f;

                        // s(t) = s* + beta (t - t*)
                        float beta = d.Dot(e) / ee;

                        float tCand = tStar - dt;
                        float sCand = sStar + beta * (tCand - tStar);
                        if (acceptS01(sCand)) keepBestT(tCand);

                        if (dt > tol) {
                            tCand = tStar + dt;
                            sCand = sStar + beta * (tCand - tStar);
                            if (acceptS01(sCand)) keepBestT(tCand);
                        }
                    }
                }
            }
            else if (nearest.solutions == 2) {
                // parallel: delta konstant
                if (std::fabs(delta - radius) <= tol) {
                    float tMin = std::min(nearest.offsets[0], nearest.offsets[1]);
                    float tMax = std::max(nearest.offsets[0], nearest.offsets[1]);
                    float upper = std::min(1.0f, limits.max);
                    float lower = limits.min;
                    float hi = std::min(tMax, upper);
                    float lo = std::max(tMin, lower);
                    if (hi >= lo - tol) keepBestT(hi); // größtes zulässiges t im Intervall
                }
            }
        }
    }

    // ---------- 2) Kappen (Endpunkte g0, g1) ----------
    if (ee > tol) {
        float tSel;
        if (CapCheckOnP(other.pts.p0, d, dd, radius, limits, tSel))
            keepBestT(tSel);
        if (CapCheckOnP(other.pts.p1, d, dd, radius, limits, tSel))
            keepBestT(tSel);
    }
    else {
        float tSel;
        if (CapCheckOnP(other.pts.p0, d, dd, radius, limits, tSel))
            keepBestT(tSel);
    }

    return collisionPoints.solutions; // 1 bei Treffer, sonst 0
}

// =================================================================================================
