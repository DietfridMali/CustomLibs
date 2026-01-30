#pragma once

#include "vector.hpp"

// =================================================================================================
// Result data for sphere-sphere collisions


namespace CollisionMath 
{
    struct ClosestPointInfo {
        Vector3f    p0{ Vector3f::NONE };
        Vector3f    p1{ Vector3f::NONE };
        float       m_distance{ 0.0f };
        bool        m_isValid{ false };
    };

    ClosestPointInfo ClosestPoint(Vector3f p0, Vector3f h0, Vector3f p1, Vector3f h1);

    bool LineContainsPoint(Vector3f p0, Vector3f p1, Vector3f p2);

    Vector3f LineLineIntersection(Vector3f p0, Vector3f h0, Vector3f p1, Vector3f h1);

    Vector3f Perpendicular2D(Vector3f v);

    Vector3f PerpendicularBase(Vector3f p0, Vector3f p1, Vector3f p2);

    float LinePointDistance2D(Vector3f lp0, Vector3f lp1, Vector3f p);

    float LinePointDistance3D(Vector3f lp0, Vector3f lp1, Vector3f p);

    Vector3f VectorSphereCollisionPoint(Vector3f p0, Vector3f p1, Vector3f p2, float s);

    CollisionInfo SphereSphereCollisionInfo(Vector3f p0, float r0, Vector3f v0, Vector3f p1, float r1, Vector3f v1);
}

// =================================================================================================
