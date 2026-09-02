#include "pch.h"
#include "indicators/internal.h"
#include "session.h"

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

namespace cmp_indicators {

double NowSec()
{
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

std::uint32_t PlayerLocationForm()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return 0;
	}
	auto* cell = player->GetParentCell();
	if (!cell) {
		return 0;
	}
	if (cell->IsInterior()) {
		return cell->GetFormID();
	}
	if (cell->worldSpace) {
		return cell->worldSpace->GetFormID();
	}
	return cell->GetFormID();
}

bool SameWorldspace(std::uint32_t youLoc, std::uint32_t peerLoc)
{
	return youLoc != 0 && peerLoc != 0 && youLoc == peerLoc;
}

float WrapPi(float a)
{
	while (a > kPi) {
		a -= 2.0f * kPi;
	}
	while (a < -kPi) {
		a += 2.0f * kPi;
	}
	return a;
}

float WorldBearingDeg(const RE::NiPoint3& from, const RE::NiPoint3& to)
{
	const float dx = to.x - from.x;
	const float dy = to.y - from.y;
	float deg = std::atan2(dx, dy) * (180.0f / kPi);
	while (deg < 0.0f) {
		deg += 360.0f;
	}
	while (deg >= 360.0f) {
		deg -= 360.0f;
	}
	return deg;
}

std::vector<PeerIndicator> CollectPeers()
{
	std::vector<PeerIndicator> out;
	auto& s = CMP_Session();
	const auto youLoc = PlayerLocationForm();
	const double now = NowSec();
	if (s.net.lastRecvPoseSec > 0.0 && (now - s.net.lastRecvPoseSec) > kPeerTimeoutSec) {
		return out;
	}

	std::uint32_t hostPeer = 0;
	{
		std::lock_guard lock(s.mutex);
		if (s.net.haveSnapshot) {
			hostPeer = s.net.lastSnapshot.hostPeerId;
		}
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	const auto you = player ? player->GetPosition() : RE::NiPoint3{};
	float nearestDist = 1.0e30f;
	std::size_t nearestIdx = 0;

	{
		std::lock_guard lock(s.mutex);
		out.reserve(s.net.latestPose.size());
		for (const auto& [peer, pose] : s.net.latestPose) {
			if (peer == s.net.myPeerId) {
				continue;
			}
			if (!SameWorldspace(youLoc, pose.locationFormId)) {
				continue;
			}
			PeerIndicator row{};
			row.peer = peer;
			row.pose = pose;
			row.isHost = hostPeer != 0 && peer == hostPeer;
			if (auto nit = s.ghosts.names.find(peer); nit != s.ghosts.names.end()) {
				row.name = nit->second;
			}
			if (row.name.empty()) {
				row.name = "peer " + std::to_string(peer);
			}
			auto git = s.ghosts.byPeer.find(peer);
			if (git != s.ghosts.byPeer.end()) {
				row.ghostHandle = git->second;
				if (const auto ptr = git->second.get()) {
					row.hasGhost = static_cast<bool>(ptr);
				}
			}
			const float dx = pose.x - you.x;
			const float dy = pose.y - you.y;
			const float dz = pose.z - you.z;
			const float dist = dx * dx + dy * dy + dz * dz;
			if (dist < nearestDist) {
				nearestDist = dist;
				nearestIdx = out.size();
			}
			out.push_back(std::move(row));
		}
	}

	if (!out.empty()) {
		out[nearestIdx].isNearest = true;
	}
	return out;
}

}  // namespace cmp_indicators
