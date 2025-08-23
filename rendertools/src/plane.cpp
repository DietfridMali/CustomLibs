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

float Plane::PointToLineDistanceEx(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2, bool clampToSegment, bool squared) {
    Vector3f v = p1;
    v -= p0;
    float l2 = v.Dot(v);

    Vector3f u = p2;
    u -= p0;

    if (l2 > m_toleranceSquared) { // otherwise line too short, compute point to point distance p0 <-> p2
        // u = p2 - (p0 + v * t) -> u = p2 - p0 - v * t;
        float t = u.Dot(v) / l2;
        if (clampToSegment)
            t = std::clamp(t, 0.0f, 1.0f);
        v *= t;
        u -= v; 
    }
    float l = u.Dot(u);
    return squared ? l : std::sqrtf(l);
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
    float denom = line.Normal().Dot(line.Velocity());
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

bool Plane::SpherePenetratesQuad(LineSegment& line, float radius) {
    // Projektion von p0 in die Ebene und Innen-Test
    if (std::fabs(planeDistance) <= radius + m_tolerance) {
        Vector3f p;
        Project(line.p0, p);
        if (Contains(pr))
            return true;
    }
    // Falls noch nicht innen: Abstand zu Kanten prüfen
    radius *= radius;
    radius += m_toleranceSquared;
    for (int i = 0; i < 4; ++i) {
        if (PointToSegmentDistanceSquared(m_vertices[i], m_vertices[(i + 1) % 4], line.p0) <= radius)
            return true;
    }
    return false;
}

// -------------------------------------------------------------------------------------------------

bool LineSegment::CapCheckOnP(const Vector3f& c, const Vector3f& d, float dd, float radius, const Conversions::FloatInterval& limits, float& tSel) const
{
    const float m_tolerance = m_tolerance;
    const float r2 = radius * radius;

    // δ_c^2 = ||(p0 - c) × d||^2 / ||d||^2
    Vector3f w = this->p0; 
    w -= c;
    Vector3f wxd = w.Cross(d);
    float delta2 = wxd.Dot(wxd) / dd;
    if (delta2 > r2 + m_tolerance) 
        return false;

    // Fußpunkt auf P und Pythagoras-Offset
    float tFoot = ScalarProjection(this->p0, c, d);
    float inside = r2 - delta2;
    float dt = (inside > 0.0f) ? std::sqrt(inside / dd) : 0.0f;

    float tA = tFoot - dt;
    float tB = tFoot + dt;

    // größtes t ≤ 1, innerhalb limits
    bool ok = false;
    tSel = -std::numeric_limits<float>::infinity();
    if (limits.Contains(tA) and tA <= 1.0f + m_tolerance) { 
        tSel = std::max(tSel, tA); 
        ok = true; 
    }
    if (std::fabs(tB - tA) > m_tolerance and limits.Contains(tB) and tB <= 1.0f + m_tolerance) { 
        tSel = std::max(tSel, tB); 
        ok = true; 
    }
    return ok;
}

// -------------------------------------------------------------------------------------------------

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
    float denom = m_normal.Dot(line.Velocity());
    if (fabs(denom) > m_tolerance) {
        float r = (d0 >= 0) ? radius : -radius;
        float t = (r - d0) / denom;
        if (limits.Contains(t)) {
            Vector3f candidate = line.p0 + line.Velocity() * t;
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
    float bestOffset = std::numeric_limits<float>::lowest(); // > permissible values
    Vector3f bestPoint;

    for (int i = 0; i < 4; ++i) {
        LineSegment edge(m_vertices[i], m_vertices[(i + 1) % 4]), collisionPoints;
        line.ComputeCapsuleIntersection(edge, collisionPoints, radius, limits);
        for (int j = 0; j < collisionPoints.solutions; ++j) {
            if (collisionPoints.offsets[j] > bestOffsets) {
                bestOffsets = collisionPoints.offsets[j];
                bestPoints = collisionPoints.endPoints[j];
            }
        }
    }

    if (bestOffset == std::numeric_limits<float>::max())
        return -1;
    if ((bestOffset < 0) and not SpherePenetratesQuad(line, radius)))
        return -1;
    collisionPoint = bestPoint;
    endPoint = line.p0 + line.Velocity() * bestOffset;
    if (line.Normal().Dot(m_normal) > 0) // just touching quad and moving away
        return 0;
    return 1;
}


#endif 

// =================================================================================================
