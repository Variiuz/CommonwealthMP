#include "server_state.hpp"

#include <cmath>

#include "log.hpp"

void tick_fake_peers(
	CmpSocket sock,
	std::unordered_map<std::string, Client>& clients,
	SessionWorld& world,
	int fakeCount,
	double t,
	std::uint64_t datagrams,
	std::uint64_t badHeaders,
	bool& fakeWasOn,
	FakeTickState& state)
{
	if ((t - state.lastFake) < (1.0 / kFakeHz)) return;
	state.lastFake = t;
	if (!fakeWasOn) state.tick = 0;
	fakeWasOn = true;
	state.angle += 0.35 / kFakeHz;
	if (state.angle > 6.28318530718) state.angle -= 6.28318530718;
	float cx = world.spawnX, cy = world.spawnY, cz = world.spawnZ, hostYaw = 0.0f;
	std::uint32_t loc = world.spawnLocation;
	if (auto* host = find_host(clients, world); host && host->havePose) {
		cx = host->lastPose.x;
		cy = host->lastPose.y;
		cz = host->lastPose.z;
		hostYaw = host->lastPose.yaw;
		loc = host->lastPose.locationFormId ? host->lastPose.locationFormId : loc;
	}
	const float omega = 0.35f;
	for (int i = 0; i < fakeCount; ++i) {
		const float phase = static_cast<float>(state.angle) + static_cast<float>(i) * (6.28318530718f / static_cast<float>(fakeCount));
		const std::uint32_t flags = cmp::fake_anim_flags(state.tick, i);
		float x = 0.0f, y = 0.0f, yaw = phase + 1.57079632679f, speed = 0.0f, vx = 0.0f, vy = 0.0f;
		if (cmp::fake_anim_holds_still(flags)) {
			const float dist = 180.0f + static_cast<float>(i) * 40.0f;
			x = cx + dist * static_cast<float>(std::sin(hostYaw));
			y = cy + dist * static_cast<float>(std::cos(hostYaw));
			yaw = hostYaw + 3.14159265359f;
		} else {
			const float radius = 180.0f + static_cast<float>(i) * 60.0f;
			speed = omega * radius;
			vx = -static_cast<float>(std::sin(phase)) * speed;
			vy = static_cast<float>(std::cos(phase)) * speed;
			x = cx + radius * static_cast<float>(std::cos(phase));
			y = cy + radius * static_cast<float>(std::sin(phase));
		}
		const auto pose = cmp::make_pose(cmp::kFakePeerBegin + static_cast<std::uint32_t>(i), loc, x, y, cz, yaw, 0.0f, speed, vx, vy, flags);
		for (auto& [_, client] : clients) send_to(sock, client.addr, &pose, sizeof(pose), "FakePose");
		if (i == 0 && (++state.poseLog % kFakeHz) == 0) {
			LOG_DEBUG("fake peer tick n=%d anim=%s x=%.1f y=%.1f clients=%zu datagrams=%llu bad=%llu",
				fakeCount, cmp::fake_anim_name(flags), x, y, clients.size(),
				static_cast<unsigned long long>(datagrams), static_cast<unsigned long long>(badHeaders));
		}
	}
	++state.tick;
}
