#include "linesegment.h"

// =================================================================================================

float LineSegment::ComputeNearestPoint(const Vector3f& p, Vector3f& nearestPoint)
{
	float len2 = properties.length * properties.length;

	// Sonderfall: Linie zu kurz
	if (len2 < m_tolerance)
		nearestPoint = p0;
	else {
		// Projektion von (p - p0) auf die Linie (p0, p1)
		float t = (p - p0).Dot(properties.velocity) / len2;
		// Punkt auf der Linie bestimmen
		nearestPoint = p0 + Velocity() * t;
		// Abstand zurückgeben
	}
	return (p - nearestPoint).Length();
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


float LineSegment::ComputeNearestPoints(LineSegment& other, LineSegment& nearestPoints) {
    Vector3f d1 = Velocity(); // Ihr Segment: p0 -> p1
    Vector3f d2 = other.Velocity(); // anderes Segment: q0 -> q1
    Vector3f r = p0 - other.p0;
    float a = d1.Dot(d1); // Länge^2 von this
    float e = d2.Dot(d2); // Länge^2 von other
    float f = d2.Dot(r);

    float s = 0.0f, t = 0.0f;
    if (a <= m_tolerance and e <= m_tolerance) {
        // Beide Segmente sind Punkte
        nearestPoints.Init(p0, other.p0);
        return (p0 - other.p0).Length();
    }
    if (a <= m_tolerance) {
        // this ist ein Punkt
        s = 0.0f;
        t = f / e;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    else {
        float c = d1.Dot(r);
        if (e <= m_tolerance) {
            // other ist ein Punkt
            t = 0.0f;
            s = -c / a;
            s = std::clamp(s, 0.0f, 1.0f);
        }
        else {
            float b = d1.Dot(d2);
            float denom = a * e - b * b;
            if (denom != 0.0f) {
                s = (b * f - c * e) / denom;
                s = std::clamp(s, 0.0f, 1.0f);
            }
            else {
                s = 0.0f; // Parallel
            }
            // Jetzt t aus s bestimmen:
            t = (b * s + f) / e;
            // Clamp t falls außerhalb
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    nearestPoints.Init(p0 + d1 * s, other.p0 + d2 * t);
    return nearestPoints.Length();
}



int LineSegment::ComputeCapsuleIntersection(LineSegment& other, LineSegment& collisionPoints, float radius, const Conversions::FloatInterval& limits) {
    const Vector3f& d = this->Velocity();
    const Vector3f& e = other.Velocity();
    Vector3f m = this->p0 - other.p0;

    float dd = d.Dot(d);
    float ee = e.Dot(e);
    float de = d.Dot(e);
    float dm = d.Dot(m);
    float em = e.Dot(m);

    float A = dd * ee - de * de;
    if (fabs(A) < m_tolerance)
        return 0; // Kein Schnitt
    float B = 2.0f * (dd * em - de * dm);
    float C = dd * (m.Dot(m) - radius * radius) - dm * dm;

    float D = B * B - 4.0f * A * C;
    collisionPoints.solutions = 0;

    if (D < 0.0f)
        return 0; // Kein Schnitt

    float sqrtD = sqrtf(D);
    float t0 = (-B - sqrtD) / (2.0f * A);
    float t1 = (-B + sqrtD) / (2.0f * A);

    collisionPoints.solutions = 0;

    // t0 prüfen
    if (limits.Contains(t0)) {
        collisionPoints.offsets[collisionPoints.solutions] = t0;
        collisionPoints.endPoints[collisionPoints.solutions] = this->p0 + d * t0;
        ++collisionPoints.solutions;
    }
    // t1 prüfen (nur, wenn nicht gleich t0)
    if (limits.Contains(t1) and (fabs(t1 - t0) > m_tolerance)) {
        collisionPoints.offsets[collisionPoints.solutions] = t1;
        collisionPoints.endPoints[collisionPoints.solutions] = this->p0 + d * t1;
        ++collisionPoints.solutions;
    }
    return collisionPoints.solutions;
}

// =================================================================================================
