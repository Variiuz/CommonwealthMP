#pragma once

#include "cmp_protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace cmp_motion {

inline constexpr std::size_t kPoseRingSize = 8;
inline constexpr float kSnapUu = 2048.0f;
inline constexpr float kMinMoveUu = 0.01f;
inline constexpr float kCatchUpMult = 2.5f;

struct TimedSample {
	double recvSec{ 0.0 };
	float x{ 0 };
	float y{ 0 };
	float z{ 0 };
	float yaw{ 0 };
	float pitch{ 0 };
	float speed{ 0 };
	float vx{ 0 };
	float vy{ 0 };
	std::uint32_t flags{ 0 };
	std::uint32_t locationFormId{ 0 };
};

struct PoseRing {
	std::deque<TimedSample> samples;

	void push(TimedSample s)
	{
		if (!samples.empty()) {
			const auto& last = samples.back();
			if (s.recvSec < last.recvSec) {
				s.recvSec = last.recvSec;
			}
			if (s.x == last.x && s.y == last.y && s.z == last.z && s.yaw == last.yaw
				&& s.speed == last.speed && s.flags == last.flags) {
				samples.back() = s;
				return;
			}
		}
		samples.push_back(s);
		while (samples.size() > kPoseRingSize) {
			samples.pop_front();
		}
	}

	void clear() { samples.clear(); }
};

struct SampledTransform {
	float x{ 0 };
	float y{ 0 };
	float z{ 0 };
	float yaw{ 0 };
	float pitch{ 0 };
	float speed{ 0 };
	float vx{ 0 };
	float vy{ 0 };
	std::uint32_t flags{ 0 };
	std::uint32_t locationFormId{ 0 };
	bool valid{ false };
	bool extrapolated{ false };
};

inline float Lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

inline float LerpAngle(float a, float b, float t)
{
	float d = b - a;
	while (d > 3.14159265f) {
		d -= 6.2831853f;
	}
	while (d < -3.14159265f) {
		d += 6.2831853f;
	}
	return a + d * t;
}

inline SampledTransform SampleAt(const PoseRing& ring, double renderSec)
{
	SampledTransform out;
	if (ring.samples.empty()) {
		return out;
	}
	if (ring.samples.size() == 1 || renderSec <= ring.samples.front().recvSec) {
		const auto& s = ring.samples.front();
		out.x = s.x;
		out.y = s.y;
		out.z = s.z;
		out.yaw = s.yaw;
		out.pitch = s.pitch;
		out.speed = s.speed;
		out.vx = s.vx;
		out.vy = s.vy;
		out.flags = s.flags;
		out.locationFormId = s.locationFormId;
		out.valid = true;
		return out;
	}
	const auto& newest = ring.samples.back();
	if (renderSec >= newest.recvSec) {
		const float dt = static_cast<float>(std::min(0.15, renderSec - newest.recvSec));
		out.x = newest.x + newest.vx * dt;
		out.y = newest.y + newest.vy * dt;
		out.z = newest.z;
		out.yaw = newest.yaw;
		out.pitch = newest.pitch;
		out.speed = newest.speed;
		out.vx = newest.vx;
		out.vy = newest.vy;
		out.flags = newest.flags;
		out.locationFormId = newest.locationFormId;
		out.valid = true;
		out.extrapolated = dt > 0.0001f;
		return out;
	}
	for (std::size_t i = 0; i + 1 < ring.samples.size(); ++i) {
		const auto& a = ring.samples[i];
		const auto& b = ring.samples[i + 1];
		if (renderSec < a.recvSec || renderSec > b.recvSec) {
			continue;
		}
		const double span = b.recvSec - a.recvSec;
		const float t = span > 1e-6 ? static_cast<float>((renderSec - a.recvSec) / span) : 1.0f;
		out.x = Lerp(a.x, b.x, t);
		out.y = Lerp(a.y, b.y, t);
		out.z = Lerp(a.z, b.z, t);
		out.yaw = LerpAngle(a.yaw, b.yaw, t);
		out.pitch = Lerp(a.pitch, b.pitch, t);
		out.speed = Lerp(a.speed, b.speed, t);
		out.vx = Lerp(a.vx, b.vx, t);
		out.vy = Lerp(a.vy, b.vy, t);
		out.flags = t < 0.5f ? a.flags : b.flags;
		out.locationFormId = b.locationFormId ? b.locationFormId : a.locationFormId;
		out.valid = true;
		return out;
	}
	out.x = newest.x;
	out.y = newest.y;
	out.z = newest.z;
	out.yaw = newest.yaw;
	out.pitch = newest.pitch;
	out.speed = newest.speed;
	out.vx = newest.vx;
	out.vy = newest.vy;
	out.flags = newest.flags;
	out.locationFormId = newest.locationFormId;
	out.valid = true;
	return out;
}

struct ApplyResult {
	float x{ 0 };
	float y{ 0 };
	float z{ 0 };
	float yaw{ 0 };
	bool snapped{ false };
};

inline ApplyResult StepToward(
	float curX,
	float curY,
	float curZ,
	float /*curYaw*/,
	const SampledTransform& target,
	float dt,
	float interpDelayMs)
{
	ApplyResult r;
	r.yaw = target.yaw;
	const float dx = target.x - curX;
	const float dy = target.y - curY;
	const float dz = target.z - curZ;
	const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (dist > kSnapUu || dist < kMinMoveUu || interpDelayMs <= 0.0f) {
		r.x = target.x;
		r.y = target.y;
		r.z = target.z;
		r.snapped = dist > kSnapUu;
		return r;
	}
	const float speed = std::max(target.speed, 40.0f);
	const float catchUp = dist > 256.0f ? kCatchUpMult : 1.35f;
	const float step = std::min(dist, speed * dt * catchUp);
	const float s = step / dist;
	r.x = curX + dx * s;
	r.y = curY + dy * s;
	r.z = curZ + dz * s;
	return r;
}

inline TimedSample FromPlayerPose(const cmp::PlayerPose& pose, double recvSec)
{
	TimedSample s;
	s.recvSec = recvSec;
	s.x = pose.x;
	s.y = pose.y;
	s.z = pose.z;
	s.yaw = pose.yaw;
	s.pitch = pose.pitch;
	s.speed = pose.speed;
	s.vx = pose.vx;
	s.vy = pose.vy;
	s.flags = pose.flags;
	s.locationFormId = pose.locationFormId;
	return s;
}

inline TimedSample FromActorPose(const cmp::ActorPose& pose, double recvSec)
{
	TimedSample s;
	s.recvSec = recvSec;
	s.x = pose.x;
	s.y = pose.y;
	s.z = pose.z;
	s.yaw = pose.yaw;
	s.pitch = pose.pitch;
	s.speed = pose.speed;
	s.vx = pose.vx;
	s.vy = pose.vy;
	s.flags = pose.flags;
	s.locationFormId = pose.locationFormId;
	return s;
}

}  // namespace cmp_motion
