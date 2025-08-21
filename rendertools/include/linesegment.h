#pragma once

#include <array>
#include "conversions.hpp"
#include "movement.h"

// =================================================================================================

class LineSegment {
private:
	float		m_tolerance;
	Movement	properties;

public:
	union {
		struct {
			Vector3f	p0;
			Vector3f	p1;
		};
		std::array<Vector3f, 2> endPoints;
	};
	int						solutions;
	std::array<float, 2>	offsets;

	LineSegment(Vector3f p0 = Vector3f::ZERO, Vector3f p1 = Vector3f::ZERO)
		: m_tolerance(Conversions::NumericTolerance)
	{ 
		Init(p0, p1);
	}


	inline void Refresh(void) {
		properties = p1 - p0;
	}

	void Init (Vector3f p0 = Vector3f::ZERO, Vector3f p1 = Vector3f::ZERO) {
		this->p0 = p0;
		this->p1 = p1;
		solutions = 0;
		Refresh();
	}


	inline Vector3f& Velocity(void) { return properties.velocity;  }

	inline float Length(void) { return properties.length; }

	inline float LengthSquared(void) { return properties.length * properties.length; }

	inline Vector3f& Normal(void) { return properties.normal; }

	inline Movement& Properties(void) { return properties; }

	float ComputeNearestPoint(const Vector3f& p, Vector3f& nearestPoint);

	int ComputeNearestPointsAt(const Vector3f& p, float radius, const Conversions::FloatInterval& limits);
	
	inline Vector3f NearestPointAt(int i) {
		return (i < solutions) ? p0 + Velocity() * offsets[i] : Vector3f::NONE;
	}

	float ComputeNearestPoints(LineSegment& other, LineSegment& nearestPoints);

	int ComputeCapsuleIntersection(LineSegment& other, LineSegment& collisionPoints, float radius, const Conversions::FloatInterval& limits);


	static inline float ScalarProjection(const Vector3f& p0, const Vector3f& p1, const Vector3f& dir) {
		float d = dir.Dot(dir);
		return (d < Conversions::NumericTolerance) ? 0.0f : (p1 - p0).Dot(dir) / d;
	}
};

// =================================================================================================
