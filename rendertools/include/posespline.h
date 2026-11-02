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
		float		speed;
	};

	static constexpr int32_t	cArcSamples = 64;
	static constexpr float		cPointTolerance = 1.0e-4f;
	static constexpr float		cAngleTolerance = 1.0e-6f;
	static constexpr float		cTimeStep = 0.002f;
	static constexpr float		cParallelTolerance = 1.0e-3f;
	static constexpr float		cMinFocusDistance = 1.0f;
	static constexpr float		cLoopDistance = 5.0f;
	static constexpr int32_t	cMinLoopKeys = 4;

	static glm::quat Rotation(const Matrix4f& orientation)
	noexcept;

	static Matrix4f Orientation(const glm::quat& rotation)
	noexcept;

	bool Setup(const AutoArray<Key>& keys);

	float StartTiming(int32_t firstKey, float speedScale, float rampTime, float swingTime);

	float Distance(float time) const;

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
	enum class ViewMode {
		Turn,
		Focus,
		Swing
	};

	struct Segment {
		Vector3f	p0;
		Vector3f	p1;
		Vector3f	t0;
		Vector3f	t1;
		glm::vec3	turns[3]{};
		glm::vec3	focusStart{};
		glm::vec3	focusEnd{};
		ViewMode	view{ ViewMode::Turn };
		float		focusSign{ 1.0f };
		float		rollStart{ 0.0f };
		float		rollEnd{ 0.0f };
		float		swingAngle{ 0.0f };
		float		length{ 0.0f };
		float		speed{ 0.0f };
	};

	AutoArray<Segment>		m_segments;
	AutoArray<glm::quat>	m_rotations;
	AutoArray<float>		m_arcLengths;
	AutoArray<float>		m_starts;
	AutoArray<float>		m_distances;
	AutoArray<float>		m_keyTimes;
	int32_t					m_firstKey{ 0 };
	float					m_swingTime{ 0.0f };
	float					m_loopStart{ 0.0f };
	float					m_duration{ 0.0f };
	bool					m_isLoop{ false };

	void SetupSegment(const AutoArray<Key>& keys, int32_t i);

	void SetupView(int32_t i);

	void SetupTurns(void);

	int32_t SegmentIndex(int32_t k) const;

	float KeyTime(int32_t i) const;

	glm::quat Turn(int32_t segment, float t) const;

	glm::quat FocusRotation(int32_t segment, float t, const Vector3f& position) const;

	float SwingProgress(int32_t i, float time) const;

	glm::quat SwingRotation(const glm::quat& rotation, int32_t k, float time) const;

	float ArcParameter(int32_t segment, float distance) const;
};

// =================================================================================================
