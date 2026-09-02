#pragma once

#include "cmp_protocol.hpp"

#include <string>
#include <unordered_map>

namespace cmp {

struct RateBucket {
	double windowStart = 0.0;
	int count = 0;
};

inline bool allow_rate(std::unordered_map<std::string, RateBucket>& buckets, const std::string& key, double t, int maxPerSec)
{
	auto& b = buckets[key];
	if (t - b.windowStart >= 1.0) {
		b.windowStart = t;
		b.count = 0;
	}
	++b.count;
	return b.count <= maxPerSec;
}

inline bool in_interest(
	std::uint32_t locA,
	float ax,
	float ay,
	float az,
	bool haveA,
	std::uint32_t locB,
	float bx,
	float by,
	float bz,
	bool haveB,
	float maxUu)
{
	if (maxUu <= 0.0f) {
		return true;
	}
	if (!haveA || !haveB) {
		return true;
	}
	if (locA != 0 && locB != 0 && locA != locB) {
		return true;
	}
	const float dx = ax - bx;
	const float dy = ay - by;
	const float dz = az - bz;
	return (dx * dx + dy * dy + dz * dz) <= (maxUu * maxUu);
}

inline bool in_interest(const PlayerPose& a, bool haveA, const PlayerPose& b, bool haveB, float maxUu)
{
	return in_interest(
		a.locationFormId, a.x, a.y, a.z, haveA,
		b.locationFormId, b.x, b.y, b.z, haveB,
		maxUu);
}

inline bool in_interest(const ActorPose& a, const PlayerPose& b, bool haveB, float maxUu)
{
	return in_interest(
		a.locationFormId, a.x, a.y, a.z, true,
		b.locationFormId, b.x, b.y, b.z, haveB,
		maxUu);
}

}  // namespace cmp
