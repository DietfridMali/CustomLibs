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

	inline void Refresh(void)
		noexcept
	{
		properties = p1 - p0;
	}

	void Init(Vector3f _p0 = Vector3f::ZERO, Vector3f _p1 = Vector3f::ZERO)
		noexcept
	{
		p0 = _p0;
		p1 = _p1;
		solutions = 0;
		Refresh();
	}

	inline Vector3f& Velocity(void)
		noexcept
	{
		return properties.velocity;
	}

	inline float Length(void)
		noexcept
	{
		return properties.length;
	}

	inline float LengthSquared(void)
		noexcept
	{
		return properties.length * properties.length;
	}

	inline Vector3f& Normal(void)
		noexcept
	{
		return properties.normal;
	}

	inline Movement& Properties(void)
		noexcept
	{
		return properties;
	}

	float Distance(const Vector3f& p)
		noexcept;

	float Project(const Vector3f& p, Vector3f& f)
		noexcept;

	int ComputeNearestPointsAt(const Vector3f& p, float radius, const Conversions::FloatInterval& limits)
		noexcept;

	inline Vector3f NearestPointAt(int i)
		noexcept
	{
		return (i < solutions) ? p0 + Velocity() * offsets[i] : Vector3f::NONE;
	}

	float ComputeNearestPoints(LineSegment& other, LineSegment& nearestPoints)
		noexcept;

	int ComputeCapsuleIntersection(LineSegment& other, LineSegment& collisionPoints, float radius, const Conversions::FloatInterval& limits)
		noexcept;

	// compute t so that q = p0 + dir * t is the foot point of a perpendicular on p0,dir through p1
	static inline float ScalarProjection(const Vector3f& p0, const Vector3f& p1, const Vector3f& dir)
		noexcept
	{
		float d = dir.Dot(dir);
		return (d < Conversions::NumericTolerance) ? 0.0f : (p1 - p0).Dot(dir) / d;
	}

private:
	bool CapCheckOnP(const Vector3f& c, const Vector3f& d, float dd, float radius, const Conversions::FloatInterval& limits, float& tSel) const
		noexcept;
};

// =================================================================================================
