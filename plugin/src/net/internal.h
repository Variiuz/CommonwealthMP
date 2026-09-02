#pragma once

#include "session.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace cmp_net {

inline double NowSec()
{
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

struct LocalWorld {
	std::uint32_t location{ 0 };
	float x{ 0 };
	float y{ 0 };
	float z{ 0 };
	float yaw{ 0 };
	float days{ 0 };
	float hour{ 0 };
	std::uint32_t weather{ 0 };
	bool inWorld{ false };
	bool interior{ false };
};

LocalWorld ReadLocalWorld();

void StartHeartbeat();
void StopHeartbeat();

extern std::atomic<double> g_lastRecvSec;

}  // namespace cmp_net
