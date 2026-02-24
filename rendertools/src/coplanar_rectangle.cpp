
#include <math.h>
#include <algorithm>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "std_defines.h"
#include "conversions.hpp"
#include "coplanar_rectangle.h"

// =================================================================================================
// Geometric computations in planes and rectangles in a plane

CoplanarRectangle::CoplanarRectangle()
noexcept
    : m_tolerance(Conversions::NumericTolerance), m_toleranceSquared(Conversions::NumericTolerance* Conversions::NumericTolerance)
{
    m_refDots[0] = m_refDots[1] = 0.0f;
}

// -------------------------------------------------------------------------------------------------

CoplanarRectangle::CoplanarRectangle(std::initializer_list<Vector3f> vertices) {
    Init(vertices);
}

// -------------------------------------------------------------------------------------------------

CoplanarRectangle& CoplanarRectangle::operator= (std::initializer_list<Vector3f> vertices) {
    Init(vertices);
    return *this;
}

// -------------------------------------------------------------------------------------------------

int CoplanarRectangle::Winding(void)
noexcept
{
    Vector3f e0 = m_coordinates[1] - m_coordinates[0];
    Vector3f e1 = m_coordinates[2] - m_coordinates[0];
    Vector3f n = e0.Cross(e1);
    return (n.Z() > 0) ? 1 : -1;
}

// -------------------------------------------------------------------------------------------------

void CoplanarRectangle::Init(std::initializer_list<Vector3f> vertices) {
    m_tolerance = Conversions::NumericTolerance;
    m_coordinates = vertices;
#if 0
    if (Winding() < 0) {
        std::swap(m_coordinates[1], m_coordinates[3]);
    }
#endif
    m_center = (m_coordinates[0] + m_coordinates[1] + m_coordinates[2] + m_coordinates[3]) * 0.25f;
    m_normal = Vector3f::Normal(m_coordinates[0], m_coordinates[1], m_coordinates[2]);
    m_normal.Negate();
    // refEdges and refDots are precomputed for faster "point inside rectangle" tests
    m_refEdges[0] = m_coordinates[1] - m_coordinates[0];
    m_refEdges[1] = m_coordinates[3] - m_coordinates[0];
    m_refDots[0] = m_refEdges[0].Dot(m_refEdges[0]) + m_tolerance;
    m_refDots[1] = m_refEdges[1].Dot(m_refEdges[1]) + m_tolerance;
}

// -------------------------------------------------------------------------------------------------

void CoplanarRectangle::Translate(Vector3f t)
noexcept
{
    m_center += t;
    // m_refEdges [0] += t;
    // m_refEdges [1] += t;
    for (auto& v : m_coordinates)
        v += t;
}

// -------------------------------------------------------------------------------------------------
// project point p onto plane (i.e. compute a point in the plane 
// so that a vector from that point to p is perpendicular to the plane)
float CoplanarRectangle::Project(const Vector3f& p, Vector3f& vCoplanarRectanglePoint)
noexcept
{
    float d = Distance(p);
    vCoplanarRectanglePoint = p - m_normal * d;
    return d;
}

// -------------------------------------------------------------------------------------------------

float CoplanarRectangle::NearestPointOnLine(const Vector3f& p0, const Vector3f& p1, Vector3f& vLinePoint)
noexcept
{
    Vector3f vLine = p1 - p0; // Richtungsvektor der Linie
    float denom = m_normal.Dot(vLine);

    if (std::abs(denom) < m_tolerance) {
        float d = m_normal.Dot(m_coordinates[0] - p0);
        vLinePoint = p0 + m_normal * (d / m_normal.Dot(m_normal));
    }
    else {
        float t = m_normal.Dot(m_coordinates[0] - p0) / denom;
        vLinePoint = p0 + vLine * t;
    }
    return Distance(vLinePoint);
}

// -------------------------------------------------------------------------------------------------
// if clampToSegment is true, the distance is computed to the line segment p0 - p1. Otherwise p0,p1 define an infinite line
// and the perpendicular distance from p2 to that line is computed

float CoplanarRectangle::PointToLineDistanceEx(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2, bool clampToSegment, bool squared)
noexcept
{
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
    return squared ? l : std::sqrt(l);
}

// -------------------------------------------------------------------------------------------------
// compute the intersection of a vector v between two points with a plane
// Will return None if v parallel to the plane or doesn't intersect with plane 
// (i.e. both points are on the same side of the plane)
// returns: -1 -> no hit, 0 -> touched at vCoplanarRectanglePoint, 1 -> penetrated at vCoplanarRectanglePoint
int CoplanarRectangle::LineIntersection(const Vector3f& p0, const Vector3f& p1, Vector3f& vCoplanarRectanglePoint)
noexcept
{
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
    float dist = m_normal.Dot(p0 - m_coordinates[0]);

    if (fabs(denom) < m_tolerance) {
        if (fabs(dist) <= m_tolerance) {
            vCoplanarRectanglePoint = p0;
            return 0;
        }
        vCoplanarRectanglePoint = Vector3f::NONE;
        return -1;
    }

    float t = -dist / denom;
    if (t < 0.0f or t > 1.0f) {
        vCoplanarRectanglePoint = Vector3f::NONE;
        return -1; // Kein Kontakt
    }
    vCoplanarRectanglePoint = p0 + vLine * t;
    return (t > 0.0f and t < 1.0f) ? 1 : 0;
}

// -------------------------------------------------------------------------------------------------
// find point on line p0 - p1 with distance d to plane
// returns: -1 -> no point found, 0: line is parallel to plane, 1: point returned in vCoplanarRectanglePoint
int CoplanarRectangle::PointOnLineAt(LineSegment& line, float d, Vector3f& vLinePoint)
noexcept
{
    float denom = line.Normal().Dot(line.Velocity());
    float dist = m_normal.Dot(line.pts.p0 - m_coordinates[0]);

    if (fabs(denom) < m_tolerance)  // Linie parallel zur Ebene
        return (fabs(dist - d) < m_tolerance) ? 0 : -1;

    float t = (d - dist) / denom;
#if 0
    if (t < 0.0f or t > 1.0f)
        return -1;
#endif
    vLinePoint = line.pts.p0 + line.Normal() * t;
    return 1;
}

// -------------------------------------------------------------------------------------------------
// barycentric method for testing whether a point lies in an arbitrarily shaped triangle
// not needed for rectangular shapes in a plane
bool CoplanarRectangle::TriangleContains(const Vector3f& p, const Vector3f& a, const Vector3f& b, const Vector3f& c)
noexcept
{
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

bool CoplanarRectangle::Contains(Vector3f& p, bool barycentric)
noexcept
{
    // barycentric method is rather computation heavy and not needed for rectangles in a plane
    if (barycentric)
        return
        TriangleContains(p, m_coordinates[0], m_coordinates[1], m_coordinates[2]) or
        TriangleContains(p, m_coordinates[0], m_coordinates[2], m_coordinates[3]);
    // (0 < AM ⋅ AB < AB ⋅ AB) ∧ (0 < AM ⋅ AD < AD ⋅ AD)
    Vector3f m = p - m_coordinates[0];
    float d = m.Dot(m_refEdges[0]);
    if ((-m_tolerance > d) or (d >= m_refDots[0]))
        return false;
    d = m.Dot(m_refEdges[1]);
    if ((-m_tolerance > d) or (d >= m_refDots[1]))
        return false;
    return true;
}

// -------------------------------------------------------------------------------------------------

bool CoplanarRectangle::SpherePenetratesQuad(LineSegment& line, float radius)
noexcept
{
    // Projektion von p0 in die Ebene und Innen-Test
    Vector3f p;
    float d = Project(line.pts.p0, p);
    if (std::fabs(d) > radius + m_tolerance)
        return false;
    if (Contains(p))
        return true;

    // Falls noch nicht innen: Abstand zu Kanten prüfen
    radius *= radius;
    radius += m_toleranceSquared;
    for (int i = 0; i < 4; ++i) {
        if (PointToSegmentDistanceSquared(m_coordinates[i], m_coordinates[(i + 1) % 4], line.pts.p0) <= radius)
            return true;
    }
    return false;
}

// -------------------------------------------------------------------------------------------------

int CoplanarRectangle::SphereIntersection(LineSegment line, float radius, Vector3f& collisionPoint, Vector3f& endPoint, Conversions::FloatInterval limits)
noexcept
{
    float d0 = Distance(line.pts.p0);
    float d1 = Distance(line.pts.p1);
    if ((d0 * d1 > 0) and (std::min(fabs(d0), fabs(d1)) > radius))
        return -1; // start and end on same side of plane and both too far away

    if (line.Length() < m_tolerance) {
        line.pts.p1 = line.pts.p0 + m_normal;
        line.Refresh();
    }

    int isPenetrating = -1;

    auto AllowMovement = [&](float t) noexcept -> bool {
        if (t >= 0.0f)
            return true;
        if (isPenetrating < 0)
            isPenetrating = SpherePenetratesQuad(line, radius);
        return isPenetrating != 0;
        };

    // 1. Schnittpunkt mit Ebene in Abstand radius prüfen (Projektion im Quad)
    float denom = m_normal.Dot(line.Velocity());
    if (fabs(denom) > m_tolerance) {
        float r = (d0 >= 0) ? radius : -radius;
        float t = (r - d0) / denom;
        if (AllowMovement(t) and limits.Contains(t)) {
            Vector3f candidate = line.pts.p0 + line.Velocity() * t;
            float d = Distance(candidate);
            Vector3f vCoplanarRectangle = candidate - m_normal * d;
            if (Contains(vCoplanarRectangle)) {
                endPoint = candidate;
                collisionPoint = vCoplanarRectangle;
                return (line.Normal().Dot(m_normal) >= 0) ? 0 : 1;
            }
        }
    }

    // 2. Kanten durchgehen
    float bestOffset = std::numeric_limits<float>::lowest(); // > permissible values
    Vector3f bestPoint;

    for (int i = 0; i < 4; ++i) {
        LineSegment edge(m_coordinates[i], m_coordinates[(i + 1) % 4]), collisionPoints;
        line.ComputeCapsuleIntersection(edge, collisionPoints, radius, limits);
        for (int j = 0; j < collisionPoints.solutions; ++j) {
            if (collisionPoints.offsets[j] > bestOffset) {
                bestOffset = collisionPoints.offsets[j];
                bestPoint = collisionPoints.vec[j];
            }
        }
    }

    if (bestOffset == std::numeric_limits<float>::lowest())
        return -1;
    if (not AllowMovement(bestOffset))
        return -1;
    collisionPoint = bestPoint;
    endPoint = line.pts.p0 + line.Velocity() * bestOffset;
    if (line.Normal().Dot(m_normal) >= 0) // just touching quad and moving away
        return 0;
    return 1;
}

// -------------------------------------------------------------------------------------------------
// Shortest distance between two line segments (P0P1 and Q0Q1)
static float SegmentSegmentDistance(Vector3f p0, Vector3f p1, Vector3f q0, Vector3f q1, float tolerance) {
    Vector3f u = p1 - p0, 
             v = q1 - q0, 
             w = p0 - q0;
    float a = u.Dot(u), 
          b = u.Dot(v), 
          c = v.Dot(v), 
          d = u.Dot(w), 
          e = v.Dot(w);
    float denom = a * c - b * b;
    float sc, tc;

    if (denom < tolerance) { // Quasi-parallel segments
        sc = 0.0f;
        tc = (b > c ? d / b : e / c);
    }
    else {
        sc = (b * e - c * d) / denom;
        tc = (a * e - b * d) / denom;
    }
    sc = std::clamp(sc, 0.0f, 1.0f);
    tc = std::clamp(tc, 0.0f, 1.0f);
	p0 += u * sc;
	q0 += v * tc;
    return (p0 - q0).Length();
}

// -------------------------------------------------------------------------------------------------

float CoplanarRectangle::SegmentDistance(Vector3f s1, Vector3f s2) noexcept {
    // rect[0] is origin, rect[1] and rect[3] define the two perpendicular axes
    Vector3f axisX = m_coordinates[1] - m_coordinates[0];
    Vector3f axisY = m_coordinates[3] - m_coordinates[0];
    float width = axisX.Length();
    float height = axisY.Length();

    // Normalize axes for projection
    Vector3f u = axisX * (1.0f / ((width > m_toleranceSquared) ? width : 1.0f));
    Vector3f v = axisY * (1.0f / ((height > m_toleranceSquared) ? height : 1.0f));
    Vector3f normal = u.Cross(v); // CoplanarRectangle normal

    // 1. Intersection & Quasi-Parallel check
    Vector3f segDir = s2 - s1;
    float denom = normal.Dot(segDir);

    if (std::abs(denom) > m_toleranceSquared) { // Not parallel
        float t = normal.Dot(m_coordinates[0] - s1) / denom;
        if ((t >= 0.0f) and (t <= 1.0f)) {
            Vector3f hit = s1 + segDir * t;
            Vector3f relHit = hit - m_coordinates[0];
            // Since edges are 90 deg, we check bounds via dot product
            float projX = relHit.Dot(u);
            float projY = relHit.Dot(v);
            if ((projX >= -m_toleranceSquared) and
                (projX <= width + m_toleranceSquared) and
                (projY >= -m_toleranceSquared) and
                (projY <= height + m_toleranceSquared)) {
                return 0.0f; // Intersection!
            }
        }
    }

    // 2. Minimum distance to the 4 boundary edges
    float minDist = std::numeric_limits<float>::max();
    for (int i = 0; i < 4; ++i) {
        minDist = std::min(minDist, SegmentSegmentDistance(s1, s2, m_coordinates[i], m_coordinates[(i + 1) % 4], m_toleranceSquared));
    }

    // 3. Endpoint projections (handles case where segment is directly above rectangle)
    auto CheckEndpoint = [&](Vector3f p) {
        Vector3f relP = p - m_coordinates[0];
        float projX = relP.Dot(u);
        float projY = relP.Dot(v);
        if ((projX >= 0.0f) and (projX <= width) and (projY >= 0.0f) and (projY <= height)) {
            return std::abs(relP.Dot(normal));
        }
        return std::numeric_limits<float>::max();
        };

    return std::min({ minDist, CheckEndpoint(s1), CheckEndpoint(s2) });
}

// -------------------------------------------------------------------------------------------------

float CoplanarRectangle::PointDistance(Vector3f p) noexcept {
	Vector3f pIntersect;
	float distToCoplanarRectangle = Project(p, pIntersect);
    if (Contains(pIntersect))
        return distToCoplanarRectangle;
    float minDist = std::numeric_limits<float>::max();
    for (int i = 0; i < 4; ++i)
        minDist = std::min(minDist, PointToLineDistanceEx(p, m_coordinates[i], m_coordinates[(i + 1) % 4], true, false));
    return minDist;
}

// =================================================================================================
