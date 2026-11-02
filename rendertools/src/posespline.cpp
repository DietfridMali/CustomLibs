#include <algorithm>
#include <cmath>

#include "posespline.h"
#include "conversions.hpp"

// =================================================================================================

static constexpr float cMaxRateScale = 3.0f;


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


static glm::vec3 ViewDirection(const glm::quat& rotation) {
	return rotation * glm::vec3(0.0f, 0.0f, -1.0f);
}


static glm::quat LookRotation(const glm::vec3& view, float roll) {
	glm::vec3 right = glm::normalize(glm::cross(view, glm::vec3(0.0f, 1.0f, 0.0f)));
	glm::vec3 up = glm::cross(right, view);
	return glm::normalize(glm::quat_cast(glm::mat3(right, up, -view))) * ExpRotation(glm::vec3(0.0f, 0.0f, 0.5f * roll));
}


static float ViewRoll(const glm::quat& rotation) {
	return 2.0f * LogRotation(glm::conjugate(LookRotation(ViewDirection(rotation), 0.0f)) * rotation).z;
}


static glm::vec3 TurnRate(const glm::vec3& before, const glm::vec3& after, float lengthBefore, float lengthAfter) {
	glm::vec3 rate(0.0f);
	float sum = lengthBefore + lengthAfter;
	if (sum <= PoseSpline::cPointTolerance)
		return rate;
	for (int32_t i = 0; i < 3; ++i) {
		if (before[i] * after[i] <= 0.0f)
			continue;
		float limit = cMaxRateScale * std::min(fabsf(before[i]), fabsf(after[i]));
		rate[i] = std::clamp((before[i] * lengthAfter + after[i] * lengthBefore) / sum, -limit, limit);
	}
	return rate;
}


static float KnotDistance(const Vector3f& a, const Vector3f& b) {
	return sqrtf((b - a).Length());
}


static void ArcTangents(Vector3f chord, Vector3f& given, Vector3f& mirrored) {
	float tangentLength = given.Length();
	if (tangentLength < PoseSpline::cPointTolerance)
		return;
	float chordLength = chord.Length();
	Vector3f direction = chord / chordLength;
	Vector3f tangent = given / tangentLength;
	float alignment = tangent.Dot(direction);
	float length = 2.0f * chordLength / (1.0f + std::max(alignment, 0.0f));
	given = tangent * length;
	mirrored = (direction * (2.0f * alignment) - tangent) * length;
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
	m_isLoop = (keyCount >= cMinLoopKeys) and ((keys[keyCount - 1].position - keys[0].position).Length() < cLoopDistance);
	m_segments.Resize(segmentCount);
	m_rotations.Resize(keyCount);
	m_arcLengths.Resize(segmentCount * (cArcSamples + 1));
	m_starts.Clear();
	m_distances.Clear();
	m_firstKey = 0;
	m_duration = 0.0f;
	for (int32_t i = 0; i < keyCount; ++i) {
		glm::quat q = glm::normalize(keys[(m_isLoop and (i + 1 == keyCount)) ? 0 : i].orientation);
		if ((i > 0) and (glm::dot(m_rotations[i - 1], q) < 0.0f))
			q = -q;
		m_rotations[i] = q;
	}
	for (int32_t i = 0; i < segmentCount; ++i)
		SetupSegment(keys, i);
	for (int32_t i = 0; i < segmentCount; ++i)
		SetupView(i);
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
		bool isTurning = (length > cPointTolerance) and (m_segments[k].view != ViewMode::Swing);
		secants[k] = isTurning ? LogRotation(glm::conjugate(m_rotations[k]) * m_rotations[k + 1]) / length : glm::vec3(0.0f);
	}
	rates[0] = m_isLoop ? TurnRate(secants[segmentCount - 1], secants[0], m_segments[segmentCount - 1].length, m_segments[0].length) : secants[0];
	rates[segmentCount] = m_isLoop ? rates[0] : secants[segmentCount - 1];
	for (int32_t k = 1; k < segmentCount; ++k)
		rates[k] = TurnRate(secants[k - 1], secants[k], m_segments[k - 1].length, m_segments[k].length);
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


void PoseSpline::SetupView(int32_t i) {
	Segment& segment = m_segments[i];
	segment.view = ViewMode::Turn;
	glm::vec3 viewStart = ViewDirection(m_rotations[i]);
	glm::vec3 viewEnd = ViewDirection(m_rotations[i + 1]);
	float flatStart = hypotf(viewStart.x, viewStart.z);
	float flatEnd = hypotf(viewEnd.x, viewEnd.z);
	if ((flatStart < cParallelTolerance) or (flatEnd < cParallelTolerance))
		return;
	float startX = viewStart.x / flatStart;
	float startZ = viewStart.z / flatStart;
	float endX = viewEnd.x / flatEnd;
	float endZ = viewEnd.z / flatEnd;
	float chordX = segment.p1.x - segment.p0.x;
	float chordZ = segment.p1.z - segment.p0.z;
	float det = endX * startZ - startX * endZ;
	if (fabsf(det) < cParallelTolerance)
		return;
	float t0 = (endX * chordZ - endZ * chordX) / det;
	float t1 = (startX * chordZ - startZ * chordX) / det;
	if ((fabsf(t0) < cMinFocusDistance) or (fabsf(t1) < cMinFocusDistance) or ((t0 > 0.0f) and (t1 < 0.0f)))
		return;
	segment.view = (t0 * t1 < 0.0f) ? ViewMode::Swing : ViewMode::Focus;
	segment.focusSign = (t0 < 0.0f) ? -1.0f : 1.0f;
	segment.focusStart = glm::vec3(segment.p0.x + t0 * startX, segment.p0.y + t0 * viewStart.y / flatStart, segment.p0.z + t0 * startZ);
	segment.focusEnd = glm::vec3(segment.focusStart.x, segment.p1.y + t1 * viewEnd.y / flatEnd, segment.focusStart.z);
	segment.rollStart = ViewRoll(m_rotations[i]);
	segment.rollEnd = ViewRoll(m_rotations[i + 1]);
	segment.swingAngle = (startZ * chordX - startX * chordZ >= 0.0f) ? float(Conversions::Pi) : -float(Conversions::Pi);
}


glm::quat PoseSpline::FocusRotation(int32_t segment, float t, const Vector3f& position) const {
	const Segment& s = m_segments[segment];
	glm::vec3 target = s.focusStart + (s.focusEnd - s.focusStart) * t;
	glm::vec3 view = (target - glm::vec3(position.x, position.y, position.z)) * s.focusSign;
	if (hypotf(view.x, view.z) < cPointTolerance)
		return Turn(segment, t);
	return LookRotation(glm::normalize(view), s.rollStart + (s.rollEnd - s.rollStart) * t);
}


float PoseSpline::SwingProgress(int32_t i, float time) const {
	float keyTime = KeyTime(i);
	float nextTime = KeyTime(i + 1);
	float end = std::min(keyTime + 0.5f * m_swingTime, nextTime);
	float begin = end - m_swingTime;
	if (begin < 0.0f) {
		begin = 0.0f;
		end = std::min(m_swingTime, nextTime);
	}
	float x = std::clamp((time - begin) / (end - begin), 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}


glm::quat PoseSpline::SwingRotation(const glm::quat& rotation, int32_t k, float time) const {
	int32_t count = m_starts.Length() - 1;
	float angle = 0.0f;
	float lift = 1.0f;
	bool isSwinging = false;
	for (int32_t i = k; (m_isLoop or (i < count)) and (KeyTime(i) - m_swingTime <= time); ++i) {
		const Segment& s = m_segments[SegmentIndex(i)];
		if (s.view != ViewMode::Swing)
			continue;
		float x = SwingProgress(i, time);
		if (x > 0.0f) {
			angle += s.swingAngle * x;
			lift *= 1.0f - 2.0f * x;
			isSwinging = true;
		}
	}
	glm::vec3 view = ViewDirection(rotation);
	if (not isSwinging or (hypotf(view.x, view.z) < cParallelTolerance))
		return rotation;
	glm::vec3 swung = ExpRotation(glm::vec3(0.0f, 0.5f * angle, 0.0f)) * glm::vec3(view.x, 0.0f, view.z);
	swung.y = view.y * lift;
	return LookRotation(glm::normalize(swung), ViewRoll(rotation));
}


glm::quat PoseSpline::Turn(int32_t segment, float t) const {
	const Segment& s = m_segments[segment];
	float v = 1.0f - t;
	glm::quat q = m_rotations[segment] * ExpRotation(s.turns[0] * (1.0f - v * v * v)) * ExpRotation(s.turns[1] * (t * t * (3.0f - 2.0f * t))) * ExpRotation(s.turns[2] * (t * t * t));
	return glm::normalize(q);
}


void PoseSpline::SetupSegment(const AutoArray<Key>& keys, int32_t i) {
	int32_t keyCount = keys.Length();
	int32_t loopCount = keyCount - 1;
	const Vector3f& p1 = keys[i].position;
	const Vector3f& p2 = keys[m_isLoop ? (i + 1) % loopCount : i + 1].position;
	Vector3f p0 = m_isLoop ? keys[(i + loopCount - 1) % loopCount].position : (i > 0) ? keys[i - 1].position : p1 * 2.0f - p2;
	Vector3f p3 = m_isLoop ? keys[(i + 2) % loopCount].position : (i + 2 < keyCount) ? keys[i + 2].position : p2 * 2.0f - p1;
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
		if (not m_isLoop and (i == 0) and (i + 2 < keyCount))
			ArcTangents(p2 - p1, segment.t1, segment.t0);
		else if (not m_isLoop and (i > 0) and (i + 2 == keyCount))
			ArcTangents(p2 - p1, segment.t0, segment.t1);
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


int32_t PoseSpline::SegmentIndex(int32_t k) const {
	return (m_firstKey + k) % m_segments.Length();
}


float PoseSpline::KeyTime(int32_t i) const {
	return (i < m_keyTimes.Length()) ? m_keyTimes[i] : KeyTime(i - m_segments.Length()) + m_duration - m_loopStart;
}


float PoseSpline::StartTiming(int32_t firstKey, float speedScale, float rampTime, float swingTime) {
	int32_t segmentCount = m_segments.Length();
	m_firstKey = std::clamp(firstKey, 0, segmentCount);
	m_swingTime = swingTime;
	int32_t leadIn = segmentCount - m_firstKey;
	int32_t count = m_isLoop ? leadIn + segmentCount : leadIn;
	m_starts.Resize(count + 1);
	m_keyTimes.Resize(count + 1);
	m_starts[0] = 0.0f;
	m_keyTimes[0] = 0.0f;
	for (int32_t k = 0; k < count; ++k)
		m_starts[k + 1] = m_starts[k] + m_segments[SegmentIndex(k)].length;
	m_distances.Clear();
	m_duration = 0.0f;
	if (count == 0)
		return 0.0f;
	float total = m_starts[count];
	float time = 0.0f;
	float distance = 0.0f;
	float speed = 0.0f;
	float rampStart = 0.0f;
	float rampLength = rampTime;
	float rampFrom = 0.0f;
	float rampTo = m_segments[SegmentIndex(0)].speed * speedScale;
	bool isBraking = false;
	int32_t k = 0;
	m_distances.Append(0.0f);
	for (;;) {
		time += cTimeStep;
		float x = std::clamp((time - rampStart) / rampLength, 0.0f, 1.0f);
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
			m_keyTimes[k] = float(m_distances.Length() - 1) * cTimeStep;
			if (not isBraking) {
				rampStart = time;
				rampFrom = speed;
				rampTo = m_segments[SegmentIndex(k)].speed * speedScale;
			}
		}
		if (not m_isLoop and not isBraking and (total - distance <= 0.5f * speed * rampTime)) {
			isBraking = true;
			rampStart = time;
			rampLength = 2.0f * (total - distance) / speed;
			rampFrom = speed;
			rampTo = 0.0f;
		}
	}
	m_duration = float(m_distances.Length() - 1) * cTimeStep;
	m_keyTimes[count] = m_duration;
	m_loopStart = m_keyTimes[leadIn];
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
	bool isRunning = m_isLoop or (time < m_duration);
	if (m_isLoop and (time > m_duration))
		time = m_loopStart + fmodf(time - m_loopStart, m_duration - m_loopStart);
	float distance = Distance(time);
	const float* starts = m_starts.Data();
	int32_t count = m_starts.Length() - 1;
	int32_t k = std::clamp(int32_t(std::upper_bound(starts + 1, starts + count + 1, distance) - starts) - 1, 0, count - 1);
	int32_t segment = SegmentIndex(k);
	const Segment& s = m_segments[segment];
	float local = std::clamp(distance - starts[k], 0.0f, s.length);
	float fraction = (s.length > cPointTolerance) ? local / s.length : 0.0f;
	position = Point(segment, ArcParameter(segment, local));
	glm::quat rotation = (s.view == ViewMode::Turn) ? Turn(segment, fraction) : FocusRotation(segment, fraction, position);
	orientation = Orientation(SwingRotation(rotation, k, time));
	return isRunning;
}

// =================================================================================================
