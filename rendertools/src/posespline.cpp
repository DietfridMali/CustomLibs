#include <algorithm>
#include <cmath>

#include "posespline.h"

// =================================================================================================

static glm::vec3 LogRotation(const glm::quat& q) {
	glm::quat r = (q.w < 0.0f) ? -q : q;
	glm::vec3 axis(r.x, r.y, r.z);
	float sine = glm::length(axis);
	if (sine < PoseSpline::cAngleTolerance)
		return glm::vec3(0.0f);
	return axis * (atan2f(sine, r.w) / sine);
}


static glm::quat ExpRotation(const glm::vec3& v) {
	float angle = glm::length(v);
	if (angle < PoseSpline::cAngleTolerance)
		return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	return glm::quat(cosf(angle), v * (sinf(angle) / angle));
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
	m_arcLengths.Resize(segmentCount * (cArcSamples + 1));
	m_starts.Clear();
	m_distances.Clear();
	m_firstKey = 0;
	m_duration = 0.0f;
	for (int32_t i = 0; i < keyCount; ++i) {
		glm::quat q = glm::normalize(keys[i].orientation);
		if ((i > 0) and (glm::dot(m_rotations[i - 1], q) < 0.0f))
			q = -q;
		m_rotations[i] = q;
	}
	for (int32_t i = 0; i < segmentCount; ++i)
		SetupSegment(keys, i);
	SetupTurns();
	return segmentCount > 0;
}


void PoseSpline::SetupTurns(void) {
	int32_t segmentCount = m_segments.Length();
	if (segmentCount == 0)
		return;
	AutoArray<glm::vec3> secants(segmentCount);
	AutoArray<glm::vec3> rates(segmentCount + 1);
	for (int32_t k = 0; k < segmentCount; ++k) {
		float length = m_segments[k].length;
		secants[k] = (length > cPointTolerance) ? LogRotation(glm::conjugate(m_rotations[k]) * m_rotations[k + 1]) / length : glm::vec3(0.0f);
	}
	rates[0] = secants[0];
	rates[segmentCount] = secants[segmentCount - 1];
	for (int32_t k = 1; k < segmentCount; ++k) {
		float before = m_segments[k - 1].length;
		float after = m_segments[k].length;
		float sum = before + after;
		rates[k] = (sum > cPointTolerance) ? (secants[k - 1] * after + secants[k] * before) / sum : glm::vec3(0.0f);
	}
	for (int32_t k = 0; k < segmentCount; ++k) {
		Segment& segment = m_segments[k];
		float third = segment.length / 3.0f;
		glm::quat start = m_rotations[k] * ExpRotation(rates[k] * third);
		glm::quat end = m_rotations[k + 1] * ExpRotation(rates[k + 1] * -third);
		segment.turns[0] = rates[k] * third;
		segment.turns[1] = LogRotation(glm::conjugate(start) * end);
		segment.turns[2] = rates[k + 1] * third;
	}
}


glm::quat PoseSpline::Turn(int32_t segment, float t) const {
	const Segment& s = m_segments[segment];
	float v = 1.0f - t;
	glm::quat q = m_rotations[segment] * ExpRotation(s.turns[0] * (1.0f - v * v * v)) * ExpRotation(s.turns[1] * (t * t * (3.0f - 2.0f * t))) * ExpRotation(s.turns[2] * (t * t * t));
	return glm::normalize(q);
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
	segment.speed = keys[i].speed;
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


float PoseSpline::StartTiming(int32_t firstKey, float speedScale, float rampTime) {
	int32_t segmentCount = m_segments.Length();
	m_firstKey = std::clamp(firstKey, 0, segmentCount);
	int32_t count = segmentCount - m_firstKey;
	m_starts.Resize(count + 1);
	m_starts[0] = 0.0f;
	for (int32_t k = 0; k < count; ++k)
		m_starts[k + 1] = m_starts[k] + m_segments[m_firstKey + k].length;
	m_distances.Clear();
	m_duration = 0.0f;
	if (count == 0)
		return 0.0f;
	float total = m_starts[count];
	float time = 0.0f;
	float distance = 0.0f;
	float speed = 0.0f;
	float rampStart = 0.0f;
	float rampFrom = 0.0f;
	float rampTo = m_segments[m_firstKey].speed * speedScale;
	bool isBraking = false;
	int32_t k = 0;
	m_distances.Append(0.0f);
	for (;;) {
		time += cTimeStep;
		float x = std::clamp((time - rampStart) / rampTime, 0.0f, 1.0f);
		float nextSpeed = rampFrom + (rampTo - rampFrom) * x * x * (3.0f - 2.0f * x);
		distance += 0.5f * (speed + nextSpeed) * cTimeStep;
		speed = nextSpeed;
		if ((distance >= total) or (isBraking and (x >= 1.0f))) {
			m_distances.Append(total);
			break;
		}
		m_distances.Append(distance);
		while ((k + 1 < count) and (distance >= m_starts[k + 1])) {
			++k;
			if (not isBraking) {
				rampStart = time;
				rampFrom = speed;
				rampTo = m_segments[m_firstKey + k].speed * speedScale;
			}
		}
		if (not isBraking and (total - distance <= 0.5f * speed * rampTime)) {
			isBraking = true;
			rampStart = time;
			rampFrom = speed;
			rampTo = 0.0f;
		}
	}
	m_duration = float(m_distances.Length() - 1) * cTimeStep;
	return m_duration;
}


float PoseSpline::Distance(float time) const {
	int32_t sampleCount = m_distances.Length();
	if (sampleCount < 2)
		return 0.0f;
	float step = std::clamp(time, 0.0f, m_duration) / cTimeStep;
	int32_t i = std::min(int32_t(step), sampleCount - 2);
	float f = std::min(step - float(i), 1.0f);
	return m_distances[i] + (m_distances[i + 1] - m_distances[i]) * f;
}


bool PoseSpline::Evaluate(float time, Vector3f& position, Matrix4f& orientation) const {
	if (m_distances.Length() < 2)
		return false;
	float distance = Distance(time);
	const float* starts = m_starts.Data();
	int32_t count = m_starts.Length() - 1;
	int32_t k = std::clamp(int32_t(std::upper_bound(starts + 1, starts + count + 1, distance) - starts) - 1, 0, count - 1);
	int32_t segment = m_firstKey + k;
	const Segment& s = m_segments[segment];
	float local = std::clamp(distance - starts[k], 0.0f, s.length);
	float fraction = (s.length > cPointTolerance) ? local / s.length : 0.0f;
	position = Point(segment, ArcParameter(segment, local));
	orientation = Orientation(Turn(segment, fraction));
	return time < m_duration;
}

// =================================================================================================
