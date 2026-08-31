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

inline bool in_interest(const PlayerPose& a, bool haveA, const PlayerPose& b, bool haveB, float maxUu)
{
	if (maxUu <= 0.0f) {
		return true;
	}
	if (!haveA || !haveB) {
		return true;
	}
	if (a.locationFormId != 0 && b.locationFormId != 0 && a.locationFormId != b.locationFormId) {
		return true;
	}
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	const float dz = a.z - b.z;
	return (dx * dx + dy * dy + dz * dz) <= (maxUu * maxUu);
}

}  // namespace cmp
