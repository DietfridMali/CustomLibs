#include <algorithm>
#include <cmath>

#include "posespline.h"

// =================================================================================================

static glm::vec3 LogRotation(const glm::quat& q) {
	glm::vec3 axis(q.x, q.y, q.z);
	float sine = glm::length(axis);
	if (sine < PoseSpline::cAngleTolerance)
		return glm::vec3(0.0f);
	return axis * (atan2f(sine, q.w) / sine);
}


static glm::quat ExpRotation(const glm::vec3& v) {
	float angle = glm::length(v);
	if (angle < PoseSpline::cAngleTolerance)
		return glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
	return glm::quat(cosf(angle), v * (sinf(angle) / angle));
}


static glm::quat Intermediate(const glm::quat& previous, const glm::quat& current, const glm::quat& next) {
	glm::quat inverse = glm::conjugate(current);
	return glm::normalize(current * ExpRotation((LogRotation(inverse * next) + LogRotation(inverse * previous)) * -0.25f));
}


static glm::quat Squad(const glm::quat& q0, const glm::quat& q1, const glm::quat& s0, const glm::quat& s1, float t) {
	return glm::normalize(glm::mix(glm::mix(q0, q1, t), glm::mix(s0, s1, t), 2.0f * t * (1.0f - t)));
}


static float KnotDistance(const Vector3f& a, const Vector3f& b) {
	return sqrtf((b - a).Length());
}

// =================================================================================================

glm::quat PoseSpline::Rotation(const Matrix4f& orientation)
noexcept
{
	return glm::normalize(glm::quat_cast(glm::mat3(orientation.m)));
}


Matrix4f PoseSpline::Orientation(const glm::quat& rotation)
noexcept
{
	return Matrix4f(glm::mat4_cast(rotation));
}


bool PoseSpline::Setup(const AutoArray<Key>& keys) {
	int32_t keyCount = keys.Length();
	int32_t segmentCount = std::max(keyCount - 1, 0);
	m_segments.Resize(segmentCount);
	m_rotations.Resize(keyCount);
	m_controls.Resize(keyCount);
	m_arcLengths.Resize(segmentCount * (cArcSamples + 1));
	m_times.Clear();
	m_slopes.Clear();
	m_firstKey = 0;
	m_duration = 0.0f;
	for (int32_t i = 0; i < keyCount; ++i) {
		glm::quat q = glm::normalize(keys[i].orientation);
		if ((i > 0) and (glm::dot(m_rotations[i - 1], q) < 0.0f))
			q = -q;
		m_rotations[i] = q;
	}
	for (int32_t i = 0; i < keyCount; ++i)
		m_controls[i] = ((i == 0) or (i == keyCount - 1)) ? m_rotations[i] : Intermediate(m_rotations[i - 1], m_rotations[i], m_rotations[i + 1]);
	for (int32_t i = 0; i < segmentCount; ++i)
		SetupSegment(keys, i);
	return segmentCount > 0;
}


void PoseSpline::SetupSegment(const AutoArray<Key>& keys, int32_t i) {
	int32_t keyCount = keys.Length();
	const Vector3f& p1 = keys[i].position;
	const Vector3f& p2 = keys[i + 1].position;
	Vector3f p0 = (i > 0) ? keys[i - 1].position : p1 * 2.0f - p2;
	Vector3f p3 = (i + 2 < keyCount) ? keys[i + 2].position : p2 * 2.0f - p1;
	Segment& segment = m_segments[i];
	segment.p0 = p1;
	segment.p1 = p2;
	segment.duration = keys[i].duration;
	if ((p2 - p1).Length() < cPointTolerance) {
		segment.t0 = Vector3f::ZERO;
		segment.t1 = Vector3f::ZERO;
	}
	else {
		float d1 = KnotDistance(p1, p2);
		float d0 = ((p1 - p0).Length() < cPointTolerance) ? d1 : KnotDistance(p0, p1);
		float d2 = ((p3 - p2).Length() < cPointTolerance) ? d1 : KnotDistance(p2, p3);
		segment.t0 = ((p1 - p0) / d0 - (p2 - p0) / (d0 + d1) + (p2 - p1) / d1) * d1;
		segment.t1 = ((p2 - p1) / d1 - (p3 - p1) / (d1 + d2) + (p3 - p2) / d2) * d1;
	}
	float* arc = m_arcLengths.Data() + i * (cArcSamples + 1);
	Vector3f previous = p1;
	arc[0] = 0.0f;
	for (int32_t k = 1; k <= cArcSamples; ++k) {
		Vector3f p = Point(i, float(k) / float(cArcSamples));
		arc[k] = arc[k - 1] + (p - previous).Length();
		previous = p;
	}
	segment.length = arc[cArcSamples];
}


Vector3f PoseSpline::Point(int32_t segment, float t) const {
	const Segment& s = m_segments[segment];
	float t2 = t * t;
	float t3 = t2 * t;
	return s.p0 * (2.0f * t3 - 3.0f * t2 + 1.0f) + s.t0 * (t3 - 2.0f * t2 + t) + s.p1 * (3.0f * t2 - 2.0f * t3) + s.t1 * (t3 - t2);
}


float PoseSpline::ArcParameter(int32_t segment, float distance) const {
	const float* arc = m_arcLengths.Data() + segment * (cArcSamples + 1);
	if (arc[cArcSamples] <= 0.0f)
		return 0.0f;
	int32_t k = std::clamp(int32_t(std::upper_bound(arc, arc + cArcSamples + 1, distance) - arc), 1, cArcSamples);
	float span = arc[k] - arc[k - 1];
	float f = (span > 0.0f) ? (distance - arc[k - 1]) / span : 0.0f;
	return (float(k - 1) + f) / float(cArcSamples);
}


float PoseSpline::StartTiming(int32_t firstKey) {
	int32_t segmentCount = m_segments.Length();
	m_firstKey = std::clamp(firstKey, 0, segmentCount);
	int32_t count = segmentCount - m_firstKey;
	m_times.Resize(count + 1);
	m_slopes.Resize(count + 1);
	m_times[0] = 0.0f;
	m_slopes[0] = 0.0f;
	m_slopes[count] = 0.0f;
	for (int32_t k = 0; k < count; ++k)
		m_times[k + 1] = m_times[k] + m_segments[m_firstKey + k].duration;
	for (int32_t k = 1; k < count; ++k) {
		const Segment& a = m_segments[m_firstKey + k - 1];
		const Segment& b = m_segments[m_firstKey + k];
		float speedA = a.length / a.duration;
		float speedB = b.length / b.duration;
		if ((speedA <= 0.0f) or (speedB <= 0.0f))
			m_slopes[k] = 0.0f;
		else {
			float weightA = 2.0f * b.duration + a.duration;
			float weightB = b.duration + 2.0f * a.duration;
			m_slopes[k] = (weightA + weightB) / (weightA / speedA + weightB / speedB);
		}
	}
	m_duration = m_times[count];
	return m_duration;
}


bool PoseSpline::Evaluate(float time, Vector3f& position, Matrix4f& orientation) const {
	int32_t count = m_times.Length() - 1;
	if (count < 1)
		return false;
	time = std::clamp(time, 0.0f, m_duration);
	const float* times = m_times.Data();
	int32_t k = std::clamp(int32_t(std::upper_bound(times + 1, times + count + 1, time) - times) - 1, 0, count - 1);
	int32_t i = m_firstKey + k;
	const Segment& segment = m_segments[i];
	float u = (time - times[k]) / segment.duration;
	float u2 = u * u;
	float u3 = u2 * u;
	float distance = (u3 - 2.0f * u2 + u) * segment.duration * m_slopes[k] + (3.0f * u2 - 2.0f * u3) * segment.length + (u3 - u2) * segment.duration * m_slopes[k + 1];
	distance = std::clamp(distance, 0.0f, segment.length);
	float fraction = (segment.length > cPointTolerance) ? distance / segment.length : u;
	position = Point(i, ArcParameter(i, distance));
	orientation = Orientation(Squad(m_rotations[i], m_rotations[i + 1], m_controls[i], m_controls[i + 1], fraction));
	return time < m_duration;
}

// =================================================================================================
