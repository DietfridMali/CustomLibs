#pragma once

#include <glm/gtc/quaternion.hpp>

#include "array.hpp"
#include "vector.hpp"
#include "matrix.hpp"

// =================================================================================================

class PoseSpline {
public:
	struct Key {
		Vector3f	position;
		glm::quat	orientation;
		float		duration;
	};

	static constexpr int32_t	cArcSamples = 64;
	static constexpr float		cPointTolerance = 1.0e-4f;
	static constexpr float		cAngleTolerance = 1.0e-6f;

	static glm::quat Rotation(const Matrix4f& orientation)
	noexcept;

	static Matrix4f Orientation(const glm::quat& rotation)
	noexcept;

	bool Setup(const AutoArray<Key>& keys);

	float StartTiming(int32_t firstKey);

	bool Evaluate(float time, Vector3f& position, Matrix4f& orientation) const;

	Vector3f Point(int32_t segment, float t) const;

	inline int32_t SegmentCount(void) const
	noexcept
	{
		return m_segments.Length();
	}

	inline float Duration(void) const
	noexcept
	{
		return m_duration;
	}

private:
	struct Segment {
		Vector3f	p0;
		Vector3f	p1;
		Vector3f	t0;
		Vector3f	t1;
		float		length{ 0.0f };
		float		duration{ 0.0f };
	};

	AutoArray<Segment>		m_segments;
	AutoArray<glm::quat>	m_rotations;
	AutoArray<glm::quat>	m_controls;
	AutoArray<float>		m_arcLengths;
	AutoArray<float>		m_times;
	AutoArray<float>		m_slopes;
	int32_t					m_firstKey{ 0 };
	float					m_duration{ 0.0f };

	void SetupSegment(const AutoArray<Key>& keys, int32_t i);

	float ArcParameter(int32_t segment, float distance) const;
};

// =================================================================================================
