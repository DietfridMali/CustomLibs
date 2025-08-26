# pragma once

#include <initializer_list>
#include <string.h>
#include <math.h>
#include "array.hpp"
#include "vector.hpp"
#include "linesegment.h"

// =================================================================================================
// Geometric computations in planes and rectangles in a plane

class Plane 
    : public Vector3f {

public:
    ManagedArray<Vector3f>  m_vertices;
    Vector3f                m_normal;
    Vector3f                m_center;
    Vector3f                m_refEdges[2];
    float                   m_refDots[2];
    float                   m_tolerance;
    float                   m_toleranceSquared;

    Plane()
        noexcept;

        Plane(std::initializer_list<Vector3f> vertices);

    Plane& operator= (std::initializer_list<Vector3f> vertices);

    void Init(std::initializer_list<Vector3f> vertices);

    int Winding(void)
        noexcept;

        // distance to plane (v0, v1, v2)
        inline float Distance(const Vector3f& v) {
        return (v - m_vertices[0]).Dot(m_normal);
    }
    /* noexcept is intentionally omitted here to keep your original single-line style for inlines;
       all other non-inline declarations below are marked with noexcept in the next line. */

       // flips the normal for collision handling with walls with thickness zero (i.e. where a wall is accessible from both of its sides)
    inline void AdjustNormal(const Vector3f& v) {
        if (Distance(v) < 0.0f)
            m_normal = -m_normal;
    }

    // project point p on this plane (i.e. compute a point in the plane 
    // so that a vector from that point to p is parallel to the plane's normal)
    float Project(const Vector3f& p, Vector3f& vPlanePoint)
        noexcept;

    float PointToLineDistanceEx(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2, bool clampToSegment, bool squared)
        noexcept;

        inline float PointToLineDistance(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2) noexcept {
        return PointToLineDistanceEx(p0, p1, p2, false, false);
    }

    inline float PointToLineDistanceSquared(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2) noexcept {
        return PointToLineDistanceEx(p0, p1, p2, false, true);
    }

    inline float PointToSegmentDistance(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2) noexcept {
        return PointToLineDistanceEx(p0, p1, p2, true, false);
    }

    inline float PointToSegmentDistanceSquared(const Vector3f& p0, const Vector3f& p1, const Vector3f& p2) noexcept {
        return PointToLineDistanceEx(p0, p1, p2, true, true);
    }

    float NearestPointOnLine(const Vector3f& p0, const Vector3f& p1, Vector3f& vLinePoint)
        noexcept;

        // compute the intersection of a vector v between two points with a plane
        // Will return None if v parallel to the plane or doesn't intersect with plane 
        // (i.e. both points are on the same side of the plane)
    int LineIntersection(const Vector3f& p0, const Vector3f& p1, Vector3f& vPlanePoint)
        noexcept;

    int SphereIntersection(LineSegment line, float radius, Vector3f& collisionPoint, Vector3f& endPoint, Conversions::FloatInterval limits)
        noexcept;

    int PointOnLineAt(LineSegment& line, float d, Vector3f& vLinePoint)
        noexcept;

        // barycentric method for testing whether a point lies in an arbitrarily shaped triangle
        // not needed for rectangular shapes in a plane
    bool TriangleContains(const Vector3f& p, const Vector3f& a, const Vector3f& b, const Vector3f& c)
        noexcept;

    bool Contains(Vector3f& p, bool barycentric = false)
        noexcept;

    void Translate(Vector3f t)
        noexcept;

    inline Vector3f& GetNormal(void) noexcept {
        return m_normal;
    }

    inline Vector3f& GetCenter(void) noexcept {
        return m_center;
    };

private:
    bool SpherePenetratesQuad(LineSegment& line, float radius)
        noexcept;
};

// =================================================================================================
