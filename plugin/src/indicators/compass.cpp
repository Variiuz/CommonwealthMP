#include "pch.h"
#include "indicators/internal.h"

#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_Value.h"

#include <cmath>
#include <vector>

namespace cmp_indicators {

RE::HUDMenu* HudMenu()
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return nullptr;
	}
	const auto menu = ui->GetMenu<RE::HUDMenu>();
	return menu.get();
}

bool HudRoot(GFxValue& root, GFxMovie*& movie)
{
	auto* hud = HudMenu();
	if (!hud || !hud->uiMovie) {
		return false;
	}
	movie = hud->uiMovie.get();
	if (hud->menuObj.IsObject()) {
		root = hud->menuObj;
		return true;
	}
	static const char* kPaths[] = {
		"_level0",
		"_level0.HUDMovieBaseInstance",
		"HUDMovieBaseInstance",
	};
	for (const char* path : kPaths) {
		GFxValue candidate;
		if (movie->GetVariable(&candidate, path) && candidate.IsObject()) {
			root = candidate;
			return true;
		}
	}
	return false;
}

bool CompassArray(GFxValue& root, GFxMovie* movie, GFxValue& out)
{
	static const char* kMembers[] = {
		"CompassTargetDataA",
		"compassTargetDataA",
	};
	for (const char* member : kMembers) {
		if (root.GetMember(member, &out) && out.IsArray()) {
			if (!g_loggedCompassPath) {
				g_loggedCompassPath = true;
				REX::INFO("indicators compass array via member {}", member);
			}
			return true;
		}
	}
	static const char* kNested[] = {
		"_level0.HUDMovieBaseInstance.CompassTargetDataA",
		"HUDMovieBaseInstance.CompassTargetDataA",
	};
	if (movie) {
		for (const char* path : kNested) {
			GFxValue candidate;
			if (movie->GetVariable(&candidate, path) && candidate.IsArray()) {
				out = candidate;
				if (!g_loggedCompassPath) {
					g_loggedCompassPath = true;
					REX::INFO("indicators compass array via path {}", path);
				}
				return true;
			}
		}
	}
	if (!g_loggedCompassFail) {
		g_loggedCompassFail = true;
		REX::WARN("indicators compass array not found");
	}
	return false;
}

void CacheCompassFrames(GFxValue& root)
{
	if (!g_compassPlayerSetFrame) {
		GFxValue frame;
		if (root.GetMember("CompassMarkerPlayerSet", &frame) && frame.IsNumber()) {
			g_compassPlayerSetFrame = static_cast<int>(frame.GetNumber());
		} else {
			g_compassPlayerSetFrame = 3;
		}
	}
	if (!g_compassQuestFrame) {
		GFxValue frame;
		if (root.GetMember("CompassMarkerQuest", &frame) && frame.IsNumber()) {
			g_compassQuestFrame = static_cast<int>(frame.GetNumber());
		} else {
			g_compassQuestFrame = 1;
		}
	}
}

bool PushCompassSlot(GFxValue& arr, double heading, double alpha, double frame, double scale)
{
	const auto n = arr.GetArraySize();
	if (!arr.PushBack(GFxValue(heading))) {
		return false;
	}
	if (!arr.PushBack(GFxValue(alpha))) {
		return false;
	}
	if (!arr.PushBack(GFxValue(frame))) {
		return false;
	}
	if (!arr.PushBack(GFxValue(scale))) {
		return false;
	}
	(void)n;
	return true;
}

void TrimCompassPeers(GFxValue& arr)
{
	if (g_compassPeerSlots == 0) {
		return;
	}
	const auto trim = static_cast<std::int32_t>(g_compassPeerSlots * kCompassStride);
	const auto size = arr.GetArraySize();
	if (size >= static_cast<std::uint32_t>(trim)) {
		arr.RemoveElements(size - static_cast<std::uint32_t>(trim), trim);
	}
	g_compassPeerSlots = 0;
}

void UpdateCompass(const std::vector<PeerIndicator>& peers)
{
	GFxValue root;
	GFxMovie* movie = nullptr;
	g_lastHudRootOk = HudRoot(root, movie);
	if (!g_lastHudRootOk) {
		g_lastCompassOk = false;
		return;
	}
	CacheCompassFrames(root);

	GFxValue arr;
	g_lastCompassOk = CompassArray(root, movie, arr);
	if (!g_lastCompassOk) {
		return;
	}

	TrimCompassPeers(arr);

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}
	const auto you = player->GetPosition();

	for (const auto& peer : peers) {
		const float heading = WorldBearingDeg(you, RE::NiPoint3{ peer.pose.x, peer.pose.y, peer.pose.z });
		const double alpha = (peer.isNearest || peer.isHost) ? 100.0 : 72.0;
		const double scale = peer.isNearest ? 112.0 : 100.0;
		const double frame = peer.isHost ? static_cast<double>(g_compassQuestFrame)
										 : static_cast<double>(g_compassPlayerSetFrame);
		if (!PushCompassSlot(arr, heading, alpha, frame, scale)) {
			REX::WARN("indicators compass push failed peer={}", peer.peer);
			break;
		}
		++g_compassPeerSlots;
	}
	g_lastCompassPeers = g_compassPeerSlots;

	root.Invoke("SetCompassMarkers");

	float headingDeg = player->GetHeading() * (180.0f / kPi);
	while (headingDeg < 0.0f) {
		headingDeg += 360.0f;
	}
	while (headingDeg >= 360.0f) {
		headingDeg -= 360.0f;
	}
	GFxValue arg(headingDeg);
	root.Invoke("UpdateCompassMarkers", nullptr, &arg, 1);
}

void ClearCompassPeers()
{
	GFxValue root;
	GFxMovie* movie = nullptr;
	if (!HudRoot(root, movie)) {
		g_compassPeerSlots = 0;
		return;
	}
	GFxValue arr;
	if (!CompassArray(root, movie, arr)) {
		g_compassPeerSlots = 0;
		return;
	}
	TrimCompassPeers(arr);
	root.Invoke("SetCompassMarkers");
}

}  // namespace cmp_indicators
