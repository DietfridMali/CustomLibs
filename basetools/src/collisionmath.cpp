
#include "conversions.hpp"
#include "collisionmath.h"
#include "tuple"

namespace CollisionMath {

    // compute intersection point of two (infinitely long) lines extending from p0 in directon v0 and from p1 in direction v1
    ClosestPointInfo ClosestPoint(Vector3f p0, Vector3f h0, Vector3f p1, Vector3f h1) {
        // Richtungsvektoren der Linien
        // Vektor zwischen den Anfangspunkten der beiden Linien
        Vector3f r = p1 - p0;

        // Kreuzprodukt der Richtungsvektoren
        Vector3f c = h0.Cross(h0);
        float l = c.Length();

        if (l < Conversions::NumericTolerance)
        {
            // Die Linien sind parallel oder identisch
            return { Vector3f::NONE, Vector3f::NONE, 0.0f, false };
        }

        // Berechnung der Parameter t und s für den Schnittpunkt
        l *= l;
        float t = (r.Cross(h1).Dot(c)) / l;
        float s = (r.Cross(h0).Dot(c)) / l;

        Vector3f a0 = p0 + h0 * t;
        Vector3f a1 = p1 + h1 * s;
        float d = (a1 - a0).Length();

        return { a0, a1, d, true };
    }



    bool LineContainsPoint(Vector3f p0, Vector3f p1, Vector3f p2) {
        return fabs((p1 - p0).Normalize().Dot((p2 - p0).Normalize())) > 0.9999f;
    }


    // intersection point of two lines (p0,h0) and (p1,h1)
    Vector3f LineLineIntersection(Vector3f p0, Vector3f h0, Vector3f p1, Vector3f h1) {
        auto i = ClosestPoint(p0, h0, p1, h1);
        return (i.m_isValid && (i.m_distance != 0)) ? i.p0 : Vector3f::NONE;
    }


    Vector3f Perpendicular2D(Vector3f v) {
        return Vector3f(-v.z, v.y, v.x).Normalize();
    }


    // compute point base on line (p0,p1) so that vector base - p2 is perpendicular to p1 - p0
    Vector3f PerpendicularBase(Vector3f p0, Vector3f p1, Vector3f p2) {
        if (LineContainsPoint(p0, p1, p2))
            return p2;
        Vector3f v = p1 - p0;
        float d = v.Length();
        if (d < Conversions::NumericTolerance)
            return p0;
        Vector3f w = p2 - p0;
        float l = v.Dot(v);
        float t = w.Dot(v) / l;
        return p0 + v * t;
    }


    public float LinePointDistance3D(Vector3f lp0, Vector3f lp1, Vector3f p) {
        Vector3f vp = p - lp0;
        Vector3f vl = lp1 - lp0;
        Vector3f cp = vp.Cross(vl);
        float ll = vl.Length;
        float cl = cp.Length;
        return (ll * cl == 0) ? vp.Length : ll / cl;
    }


    float LinePointDistance2D(Vector3f lp0, Vector3f lp1, Vector3f p) {
        Vector3f v = lp1 - lp0;
        Vector3f w = p - lp0;
        return (float)(fabs(v.x * w.z - v.z * w.x) / sqrt(v.x * v.x + v.z * v.z));
    }

    // compute point on line (p0,p1) that is at distance s to point p2
    Vector3f VectorSphereCollisionPoint(Vector3f p0, Vector3f p1, Vector3f p2, float s) {
        Vector3f b = PerpendicularBase(p0, p1, p2);
        float l = (p2 - b).Length();
        float d = (float)sqrt(s * s - l * l);
        return b - (p1 - p0).Normalize() * d;
    }


    // collision point of two spheres with centers p0 and p1, radii r0 and r1, moving in directions v0 and v1
    // where the speed is determined by v0.Length and v1.Length resp.
    // endPoint0 is the position of the center of the first sphere and endPoint1 is the position of the center
    // of the second sphere when colliding. collisionPoint is the point where the two spheres collide
    CollisionInfo SphereSphereCollisionInfo(Vector3f p0, float r0, Vector3f v0, Vector3f p1, float r1, Vector3f v1) {
        Vector3f v = v1 - v0;
        float a = v.Dot(v);
        if (a == 0.0)
            return CollisionInfo(); // m_haveCollision is false by default
        Vector3f p = p1 - p0;
        float b = 2 * p.Dot(v);
        float r = r0 + r1;
        float c = p.Dot(p) - r * r;
        float d = b * b - 4 * a * c;
        if (d < 0)
            return CollisionInfo();
        d = (float)sqrt(d);
        float n = 2 * a;
        float t1 = (-b + d) / n;
        float t2 = (-b - d) / n;
        float t = (t1 >= 0 && t2 >= 0) ? std::min(t1, t2) : std::max(t1, t2);
        if (t < 0)
            return CollisionInfo();
        Vector3f e0 = p0 + v0 * t;
        Vector3f e1 = p1 + v1 * t;
        v = (e0 - e1).Normalize() * r1;
        return CollisionInfo(e0, e1, e1 + v, t);
    }

}