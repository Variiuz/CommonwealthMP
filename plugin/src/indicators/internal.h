#pragma once

#include "session.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Scaleform {
namespace GFx {
class Value;
class Movie;
}
}

namespace cmp_indicators {

using GFxValue = Scaleform::GFx::Value;
using GFxMovie = Scaleform::GFx::Movie;

constexpr float kPi = 3.14159265358979323846f;
constexpr double kPeerTimeoutSec = 5.0;
constexpr std::uint32_t kCompassStride = 4;

struct PeerIndicator {
	std::uint32_t peer{ 0 };
	cmp::PlayerPose pose{};
	std::string name;
	bool hasGhost{ false };
	RE::ObjectRefHandle ghostHandle{};
	bool isHost{ false };
	bool isNearest{ false };
};

extern std::size_t g_compassPeerSlots;
extern std::vector<RE::ObjectRefHandle> g_mapMarkerHandles;
extern std::unordered_map<std::uint32_t, RE::ObjectRefHandle> g_peerMarkerRefs;
extern int g_compassPlayerSetFrame;
extern int g_compassQuestFrame;
extern bool g_loggedCompassPath;
extern bool g_loggedCompassFail;
extern bool g_lastHudRootOk;
extern bool g_lastCompassOk;
extern std::size_t g_lastCompassPeers;
extern std::size_t g_lastMapMarkers;

double NowSec();
std::uint32_t PlayerLocationForm();
bool SameWorldspace(std::uint32_t youLoc, std::uint32_t peerLoc);
float WrapPi(float a);
float WorldBearingDeg(const RE::NiPoint3& from, const RE::NiPoint3& to);
std::vector<PeerIndicator> CollectPeers();

RE::HUDMenu* HudMenu();
bool HudRoot(GFxValue& root, GFxMovie*& movie);
bool CompassArray(GFxValue& root, GFxMovie* movie, GFxValue& out);
void CacheCompassFrames(GFxValue& root);
bool PushCompassSlot(GFxValue& arr, double heading, double alpha, double frame, double scale);
void TrimCompassPeers(GFxValue& arr);
void UpdateCompass(const std::vector<PeerIndicator>& peers);
void ClearCompassPeers();

void RemoveMapMarkerHandle(RE::PlayerCharacter* player, const RE::ObjectRefHandle& handle);
void ClearMapMarkers();
void UpdateMapMarkers(const std::vector<PeerIndicator>& peers);

}  // namespace cmp_indicators
