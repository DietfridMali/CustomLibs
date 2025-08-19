#include <algorithm>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "conversions.hpp"
#include "plane.h"

// =================================================================================================
// Geometric computations in planes and rectangles in a plane

Plane::Plane() 
    : m_tolerance(Conversions::NumericTolerance)
{
    m_refDots[0] = m_refDots[1] = 0.0f;
}

// -------------------------------------------------------------------------------------------------

Plane::Plane(std::initializer_list<Vector3f> vertices) {
    Init (vertices);
}

// -------------------------------------------------------------------------------------------------

Plane& Plane::operator= (std::initializer_list<Vector3f> vertices) {
    Init (vertices);
    return *this;
}

// -------------------------------------------------------------------------------------------------

int Plane::Winding(void) {
    Vector3f e0 = m_vertices[1] - m_vertices[0];
    Vector3f e1 = m_vertices[2] - m_vertices[0];
    Vector3f n = e0.Cross(e1);
    return (n.Z() > 0) ? 1 : -1;
}

// -------------------------------------------------------------------------------------------------

void Plane::Init (std::initializer_list<Vector3f> vertices) {
    m_tolerance = Conversions::NumericTolerance;
    m_vertices = vertices;
#if 0
    if (Winding() < 0) {
        std::swap(m_vertices[1], m_vertices[3]);
    }
#endif
    m_center = (m_vertices [0] + m_vertices [1] + m_vertices [2] + m_vertices [3]) * 0.25f;
    m_normal = Vector3f::Normal (m_vertices [0], m_vertices [1], m_vertices [2]);
    m_normal.Negate();
    // refEdges and refDots are precomputed for faster "point inside rectangle" tests
    m_refEdges [0] = m_vertices [1] - m_vertices [0];
    m_refEdges [1] = m_vertices [3] - m_vertices [0];
    m_refDots [0] = m_refEdges [0].Dot (m_refEdges [0]) + m_tolerance;
    m_refDots [1] = m_refEdges [1].Dot (m_refEdges [1]) + m_tolerance;
}

// -------------------------------------------------------------------------------------------------

void Plane::Translate(Vector3f t) {
    m_center += t;
    // m_refEdges [0] += t;
    // m_refEdges [1] += t;
    for (auto& v : m_vertices)
        v += t;
}

// -------------------------------------------------------------------------------------------------
// project point p onto plane (i.e. compute a point in the plane 
// so that a vector from that point to p is perpendicular to the plane)
float Plane::Project(const Vector3f& p, Vector3f& vPlanePoint) {
    float d = Distance(p);
    vPlanePoint = p - m_normal * d;
    return d; 
}

// -------------------------------------------------------------------------------------------------

float Plane::NearestPointOnLine(const Vector3f& p0, const Vector3f& p1, Vector3f& vLinePoint) {
    Vector3f vLine = p1 - p0; // Richtungsvektor der Linie
    float denom = m_normal.Dot(vLine);

    if (std::abs(denom) < m_tolerance) {
        float d = m_normal.Dot(m_vertices[0] - p0);
        vLinePoint = p0 + m_normal * (d / m_normal.Dot(m_normal));
        }
    else {
        float t = m_normal.Dot(m_vertices[0] - p0) / denom;
        vLinePoint = p0 + vLine * t;
    }
    return Distance(vLinePoint);
}

// -------------------------------------------------------------------------------------------------
// compute the intersection of a vector v between two points with a plane
// Will return None if v parallel to the plane or doesn't intersect with plane 
// (i.e. both points are on the same side of the plane)
// returns: -1 -> no hit, 0 -> touched at vPlanePoint, 1 -> penetrated at vPlanePoint
int Plane::LineIntersection(const Vector3f& p0, const Vector3f& p1, Vector3f& vPlanePoint) {
    Vector3f vLine = p1 - p0;
#if 0 // process an optional offset - not offered in interface right now though
    if (r > 0) {
        Vector3f vOffset = vLine;
        vOffset.Normalize();
        vOffset *= r;
        vLine += vOffset;
    }
#endif
    float denom = m_normal.Dot(vLine);
    float dist = m_normal.Dot(p0 - m_vertices[0]);

    if (fabs(denom) < m_tolerance) {
        if (fabs(dist) <= m_tolerance) {
            vPlanePoint = p0;
            return 0;
        }
        vPlanePoint = Vector3f::NONE;
        return -1;
    }

    float t = -dist / denom;
    if (t < 0.0f or t > 1.0f) {
        vPlanePoint = Vector3f::NONE;
        return -1; // Kein Kontakt
        }
    vPlanePoint = p0 + vLine * t;
    return (t > 0.0f and t < 1.0f) ? 1 : 0;
}

// -------------------------------------------------------------------------------------------------
// find point on line p0 - p1 with distance d to plane
// returns: -1 -> no point found, 0: line is parallel to plane, 1: point returned in vPlanePoint
int Plane::PointOnLineAt(LineSegment& line, float d, Vector3f& vLinePoint) {
    float denom = line.Normal().Dot(line.Direction());
    float dist = m_normal.Dot(line.p0 - m_vertices[0]);

    if (fabs(denom) < m_tolerance)  // Linie parallel zur Ebene
        return (fabs(dist - d) < m_tolerance) ? 0 : -1;

    float t = (d - dist) / denom;
#if 0
    if (t < 0.0f or t > 1.0f) 
        return -1;
#endif
    vLinePoint = line.p0 + line.Normal() * t;
    return 1;
}

// -------------------------------------------------------------------------------------------------
// barycentric method for testing whether a point lies in an arbitrarily shaped triangle
// not needed for rectangular shapes in a plane
bool Plane::TriangleContains(const Vector3f& p, const Vector3f& a, const Vector3f& b, const Vector3f& c) {
    Vector3f ab = b - a;
    Vector3f bc = c - b;
    Vector3f ca = a - c;

    Vector3f ap = p - a;
    Vector3f bp = p - b;
    Vector3f cp = p - c;

    Vector3f n1 = ab.Cross(ap);
    Vector3f n2 = bc.Cross(bp);
    Vector3f n3 = ca.Cross(cp);

    float d1 = n1.Dot(n2);
    float d2 = n2.Dot(n3);
    return (d1 >= 0) and (d2 >= 0);
}

// -------------------------------------------------------------------------------------------------

bool Plane::Contains(Vector3f& p, bool barycentric) {
    // barycentric method is rather computation heavy and not needed for rectangles in a plane
    if (barycentric)
        return 
            TriangleContains(p, m_vertices[0], m_vertices[1], m_vertices[2]) or
            TriangleContains(p, m_vertices[0], m_vertices[2], m_vertices[3]);
    // (0 < AM ⋅ AB < AB ⋅ AB) ∧ (0 < AM ⋅ AD < AD ⋅ AD)
    Vector3f m = p - m_vertices[0];
    float d = m.Dot(m_refEdges[0]);
    if ((-m_tolerance > d) or (d >= m_refDots[0]))
        return false;
    d = m.Dot(m_refEdges[1]);
    if ((-m_tolerance > d) or (d >= m_refDots[1]))
        return false;
    return true;
}

// -------------------------------------------------------------------------------------------------

#if 0

inline bool Plane::SolveQuadratic(float a, float b, float c, float& t0, float& t1)
{
    float discr = b * b - 4 * a * c;
    if (discr < 0) 
        return false;
    float sqrtDiscr = std::sqrt(discr);
    float inv2a = 0.5f / a;
    t0 = (-b - sqrtDiscr) * inv2a;
    t1 = (-b + sqrtDiscr) * inv2a;
    if (t0 > t1) std::swap(t0, t1);
    return true;
}


// Kugel gegen Liniensegment (edge) analytisch: Liefert frühesten t in [0,1] mit Abstand == r
float Plane::SweepSphereEdge(const Vector3f& p0, const Vector3f& vLine, const Vector3f& a, const Vector3f& b, float r)
{
    Vector3f e = b - a;       // Kante

    Vector3f w0 = p0 - a;
    float vv = vLine.Dot(vLine);
    float ee = e.Dot(e);
    float ve = vLine.Dot(e);

    float denom = vv * ee - ve * ve;
    if (std::abs(denom) < 1e-6f)
        return -1.0f; // Parallel

    // Parameter s und t mit Minimalabstand berechnen
    // Abstandsquadrat: |w0 + t*vLine - s*e|^2 = r^2, s in [0,1]
    // => d/ds=0, löst s in t auf (s = clamp(...))
    // Setze für jedes t (zwei Lösungen, Eintritt/Austritt)
    float a_ = vv * ee - ve * ve;
    float b_ = 2.0f * (w0.Dot(vLine) * ee - w0.Dot(e) * ve);
    float c_ = w0.Dot(w0) * ee - 2.0f * w0.Dot(e) * w0.Dot(e) + w0.Dot(e) * w0.Dot(e) - r * r * ee;

    float t0, t1;
    if (not SolveQuadratic(a_, b_, c_, t0, t1))
        return -1.0f;

    // Teste beide Lösungen
    for (float tCandidate : {t0, t1}) {
        if (tCandidate < 0.0f or tCandidate > 1.0f) 
            continue;
        // Berechne s für diesen t, clamp auf [0,1]
        Vector3f c = p0 + vLine * tCandidate;
        Vector3f ap = c - a;
        float s = e.Dot(ap) / ee;
        s = std::clamp(s, 0.0f, 1.0f);
        Vector3f q = a + e * s;
        float dist2 = (c - q).LengthSquared();
        if (std::abs(dist2 - r * r) < m_tolerance)
            return tCandidate;
        }
    return -1.0f;
}


// Kugel gegen Punkt analytisch: Liefert frühesten t in [0,1] mit Abstand == r
float Plane::SweepSpherePoint(const Vector3f& p0, const Vector3f& vLine, const Vector3f& c, float r)
{
    Vector3f w = p0 - c;
    float a = vLine.Dot(vLine);
    float b = 2.0f * w.Dot(vLine);
    float c_ = w.Dot(w) - r * r;

    float t0, t1;
    if (not SolveQuadratic(a, b, c_, t0, t1))
        return -1.0f;
    for (float tCandidate : {t0, t1}) {
        if (tCandidate < 0.0f or tCandidate > 1.0f) 
            continue;
        Vector3f pos = p0 + vLine * tCandidate;
        float dist2 = (pos - c).LengthSquared();
        if (std::abs(dist2 - r * r) < m_tolerance) 
            return tCandidate;
        }
    return -1.0f;
}


bool Plane::SphereIntersection(lineSegment line, float r, Vector3f& vCollide, Vector3f& vEndPoint)
{
    float tCollide = std::numeric_limits<float>::max();
    Vector3f vFirstCollide;

    // Fläche
    float d0 = Distance(line.p0);
    float d1 = Distance(line.p1);

    if ((d0 * d1 > 0.0f) and (m_normal.Dot(line.Direction()) > 0))
        return false; // both points on same side of plane and moving away
    float delta = d1 - d0;
    if (std::abs(delta) > m_tolerance) {
        float t1 = (r - d0) / delta;
        float t2 = (-r - d0) / delta;
        for (float tEntry : {t1, t2}) {
            if ((tEntry >= 0.0f) and (tEntry <= 1.0f) and (tEntry < tCollide)) {
                Vector3f centerHit = line.p0 + line.Normal() * tEntry;
                Vector3f planeHit = centerHit - m_normal * Distance(centerHit);
                if (Contains(planeHit)) {
                    tCollide = tEntry;
                    vFirstCollide = centerHit;
                }
            }
        }
    }

    // Kanten
    const auto UpdateCollision = [&](float t) {
        if ((t >= 0.0f) and (t <= 1.0f) and (t < tCollide)) {
            tCollide = t;
            vFirstCollide = line.p0 + line.Normal() * t;
            }
        };


    for (int i = 0; i < 4; ++i) {
        UpdateCollision(SweepSphereEdge(line.p0, line.Direction(), m_vertices[i], m_vertices[(i + 1) % 4], r));
    }
    // Ecken
    for (int i = 0; i < 4; ++i) {
        UpdateCollision(SweepSpherePoint(line.p0, line.Direction(), m_vertices[i], r));
    }

    if (tCollide > 1.0f)
        return false;

    float d = m_normal.Dot(line.Direction());
    if ((tCollide < m_tolerance) and (m_normal.Dot(line.Normal()) > 0)) // at start of movement, moving away from wall
        return false;

    vCollide = vFirstCollide;
    vEndPoint = line.p0 + line.Normal() * tCollide;
    tCollide = tCollide;
    return true;
}

#elif 0 // -------------------------------------------------------------------------------------------------
// returns collision point of sphere with quad closest to p0 (start point of movement) in collisionPoint
// and point on vector p0,p1 at which that collision occurs in endPoint. That point may lie behind p0 as
// seen from p1 (i.e. requires to move backwards).
// If p0,p1 penetrates the plane, collisions will only be checked from p0 to the penetration point.
// if p0 ~= p1, use projection point of p0 on plane as p1 (forces a perpendicular move away from the plane 
// if the sphere is too close, but not moving)
// function returns: -1 no collision, 0: collision, but moving away, 1: collision and moving towards quad
int Plane::SphereIntersection(LineSegment line, float radius, Vector3f& collisionPoint, Vector3f& endPoint)
{
    Vector3f dir = p1 - p0;
    float dirLen = dir.Length();

    // Spezialfall: Gerade zu kurz
    if (dirLen > m_tolerance)
        dir /= dirLen;
    else {
        float d = Project(p0, collisionPoint); // collisionPoint = Lotfußpunkt auf die Ebene
        if (fabs(d) < m_tolerance) {
            // p0 liegt bereits auf der Ebene, normale zeigt sinnhaft raus
            endPoint = collisionPoint + m_normal * radius;
            return 1;
        }
        // Sonst wie gehabt: Gerade als Lot weiterverarbeiten
        p1 = collisionPoint;
        dir = p1 - p0;
        dirLen = dir.Length();
        if (dirLen < m_tolerance)
            return -1;
        dir /= dirLen;
    }

    // Prüfen, ob Gerade die Ebene schneidet (Schnittpunkt bestimmen)
    float d0 = Distance(p0);
    float d1 = Distance(p1);
    fprintf(stderr, "wall distances = %1.6f / %1.6f, side = %d\n", d0, d1, (d0 < 0.0f) ? -1 : 1);
    if (d0 * d1 > 0) {
        if (std::min(fabs(d0), fabs(d1)) > radius) {
            collisionPoint = Vector3f::NONE;
            endPoint = p1;
            return -1;
        }
    }
    else {
        float t = d0 / (d0 - d1);
        p1 = p0 + (p1 - p0) * t;
    }

    // Minimalabstand Linie ↔ Quad bestimmen (Kanten, Ecken, Fläche)
    Vector3f qMin, gMin;
    float minDist2 = std::numeric_limits<float>::max();

    // 1. Projektion auf die Ebene (Flächenlot)
    Vector3f vPlane;
    float dp = Project(p0, vPlane);
    if (Contains(vPlane)) {
        Vector3f g;
        NearestPointOnLineToPoint(vPlane, p0, p1, g);
        float d2 = (vPlane - g).LengthSquared();
        if (d2 < minDist2) {
            minDist2 = d2;
            qMin = vPlane;
            gMin = g;
        }
    }

    // 2. Kanten
    for (int i = 0; i < 4; ++i) {
        Vector3f s0 = m_vertices[i];
        Vector3f s1 = m_vertices[(i + 1) % 4];
        Vector3f g, s;
        NearestPointOnLineToSegment(p0, p1, s0, s1, g, s);
        float d2 = (g - s).LengthSquared();
        if (d2 < minDist2) {
            minDist2 = d2;
            qMin = s;
            gMin = g;
        }
    }

    // 3. Ecken
    for (int i = 0; i < 4; ++i) {
        Vector3f g;
        NearestPointOnLineToPoint(m_vertices[i], p0, p1, g);
        float d2 = (g - m_vertices[i]).LengthSquared();
        if (d2 < minDist2) {
            minDist2 = d2;
            qMin = m_vertices[i];
            gMin = g;
        }
    }

    // Kein Treffer: Abstand zu groß?
    float minDist = sqrtf(minDist2);
    if (minDist - radius > m_tolerance)
        return -1;

    // Parallelitätsprüfung: Gerade annähernd parallel zur Ebene?
    if (fabs(dir.Dot(m_normal)) < m_tolerance) {
        // pr soll möglichst nahe bei p0 liegen (minimiere |pr - p0| unter |pr - qMin| = r)
        Vector3f v = dir.Normal() * r;
        // Punkt auf Geraden mit Abstand r zu qMin:
        // pr = gMin ± v * r, wähle Richtung, dass |pr - p0| minimal ist
        Vector3f pr1 = gMin + v;
        Vector3f pr2 = gMin - v;

        float d1 = (pr1 - p0).LengthSquared();
        float d2 = (pr2 - p0).LengthSquared();

        endPoint = (d1 < d2) ? pr1 : pr2;
        collisionPoint = qMin;
        return 1; // Da explizit der nächstliegende Punkt zu p0 gesucht ist
    }


    // Berechne den Punkt auf der Geraden mit Abstand radius zum Quad
    Vector3f vQuad = qMin - gMin;
    float quadDist = vQuad.Length();

    if (quadDist < m_tolerance) {
        // Richtung der Suche: auf Seite von p0
        endPoint = qMin + m_normal * ((d0 >= 0.0f) ? radius : -radius);
        collisionPoint = qMin;
    }
    else {
        float s = sqrtf(radius * radius - minDist2);
        float tdir = dir.Dot(p0 - gMin);
        if (tdir < 0) s = -s; // In Richtung p0 (rückwärts)
        endPoint = gMin + dir * s;
        collisionPoint = qMin;
    }

    // Prüfe, ob Richtung von p0 zu p1 auf das Quad zu zeigt
    if ((p1 - collisionPoint).Length() - (p0 - collisionPoint).Length() > m_tolerance)
        return 0; // Richtung weg vom Quad

    return 1; // Richtung auf Quad zu (oder gleichbleibend)
}

#else // -------------------------------------------------------------------------------------------------

int Plane::SphereIntersection(LineSegment line, float radius, Vector3f& collisionPoint, Vector3f& endPoint, Conversions::FloatInterval limits)
{
    float d0 = Distance(line.p0);
    float d1 = Distance(line.p1);
    if ((d0 * d1 > 0) and (std::min(fabs(d0), fabs(d1)) > radius))
        return -1; // start and end on same side of plane and both too far away

    if (line.Length() < m_tolerance) {
        line.p1 = line.p0 + m_normal;
        line.Refresh();
    }

    // 1. Schnittpunkt mit Ebene in Abstand radius prüfen (Projektion im Quad)
    float denom = m_normal.Dot(line.Direction());
    if (fabs(denom) > m_tolerance) {
        float r = (d0 >= 0) ? radius : -radius;
        float t = (r - d0) / denom;
        if (limits.Contains(t)) {
            Vector3f candidate = line.p0 + line.Direction() * t;
            float d = Distance(candidate);
            Vector3f vPlane = candidate - m_normal * d;
            if (Contains(vPlane)) {
                endPoint = candidate;
                collisionPoint = vPlane;
                return (line.Normal().Dot(m_normal) > 0) ? 0 : 1;
            }
        }
    }

    // 2. Kanten durchgehen
    LineSegment bestPoints;
    bestPoints.offsets = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max() }; // > permissible values

    for (int i = 0; i < 4; ++i) {
        LineSegment edge(m_vertices[i], m_vertices[(i + 1) % 4]), collisionPoints;
        line.ComputeCapsuleIntersection(edge, collisionPoints, radius, limits);
        for (int j = 0; j < collisionPoints.solutions; ++j) {
            if (collisionPoints.offsets[j] < 0.0f) {
                if (collisionPoints.offsets[j] > bestPoints.offsets[0]) {
                    bestPoints.offsets[0] = collisionPoints.offsets[j];
                    bestPoints.p0 = collisionPoints.endPoints[j];
                }
            }
            else {
                if (collisionPoints.offsets[j] < bestPoints.offsets[1]) {
                    bestPoints.offsets[1] = collisionPoints.offsets[j];
                    bestPoints.p1 = collisionPoints.endPoints[j];
                }
            }
        }
    }

    if (bestPoints.offsets[1] != std::numeric_limits<float>::max()) {
        t = bestPoints.offsets[1];
        collisionPoint = bestPoints.p1;
    }
    else if (bestPoints.offsets[0] != std::numeric_limits<float>::lowest()) {
        t = bestPoints.offsets[0];
        collisionPoint = bestPoints.p0;
        }
    else
        return -1;

    endPoint = line.p0 + line.Direction() * t;
    if (line.Normal().Dot(m_normal) > 0) // just touching quad and moving away
        return 0;
    return 1;
}


#endif 

// =================================================================================================
