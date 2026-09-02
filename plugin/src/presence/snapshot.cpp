#include "pch.h"
#include "presence/internal.h"
#include "session.h"
#include "menu.h"
#include "steam_api.h"

#include <cstdint>
#include <string>

namespace cmp_presence {

std::uint64_t HashSnapshot(const PresenceSnapshot& snap)
{
	std::uint64_t h = 14695981039346656037ull;
	auto mix = [&](const std::string& s) {
		for (unsigned char c : s) {
			h ^= c;
			h *= 1099511628211ull;
		}
	};
	mix(std::to_string(static_cast<int>(snap.phase)));
	mix(snap.details);
	mix(snap.state);
	mix(snap.steamStatus);
	h ^= snap.playerCount;
	h ^= static_cast<std::uint64_t>(snap.maxPlayers) << 32;
	return h;
}

std::string LocationText(bool joined, bool interior, std::uint32_t worldspace)
{
	if (!joined) {
		return {};
	}
	if (interior) {
		return "Interior";
	}
	if (worldspace == cmp::kCommonwealthWorldspace) {
		return "Commonwealth";
	}
	if (worldspace != 0) {
		return "Wasteland";
	}
	return "Unknown";
}

PresenceSnapshot BuildSnapshot()
{
	auto& s = CMP_Session();
	PresenceSnapshot snap{};

	std::string menuPhase;
	bool menuActive = false;
	{
		menuPhase = CMP_MenuPresencePhase(menuActive);
	}

	std::lock_guard lock(s.mutex);
	snap.playerCount = static_cast<std::uint32_t>(s.net.latestPose.size());
	snap.maxPlayers = s.presence.maxPlayers;

	const std::string serverName = !s.presence.serverName.empty() ? s.presence.serverName : "CMP";
	const bool connecting = s.net.joined && s.net.myPeerId == 0;
	const bool interior = s.presence.interior;
	const std::uint32_t worldspace = s.presence.worldspace;
	const std::string location = LocationText(s.net.joined, interior, worldspace);

	if (menuActive) {
		snap.phase = PresencePhase::MenuJoin;
		snap.details = "CMP";
		snap.state = menuPhase.empty() ? "Joining" : menuPhase;
		snap.steamStatus = snap.state;
	} else if (connecting || (!s.net.joined && s.lastStatus.find("waiting Welcome") != std::string::npos)) {
		snap.phase = PresencePhase::Connecting;
		snap.details = "CMP";
		snap.state = "Connecting...";
		snap.steamStatus = "Connecting";
	} else if (s.net.joined && s.net.myPeerId != 0) {
		const std::uint32_t others = snap.playerCount > 0 ? snap.playerCount - 1 : 0;
		const std::uint32_t cap = snap.maxPlayers > 0 ? snap.maxPlayers : snap.playerCount;
		if (s.net.isHost) {
			snap.phase = PresencePhase::Host;
			snap.details = serverName;
			snap.state = "Hosting (" + std::to_string(snap.playerCount) + "/" + std::to_string(cap) + " players)";
			if (!location.empty()) {
				snap.state += " - " + location;
			}
			snap.steamStatus = "Hosting";
		} else {
			snap.phase = PresencePhase::Guest;
			snap.details = serverName;
			snap.state = "Playing (" + std::to_string(others) + " others)";
			if (!location.empty()) {
				snap.state += " - " + location;
			}
			snap.steamStatus = "Playing";
		}
	} else {
		snap.phase = PresencePhase::Idle;
		snap.details = "CMP";
		snap.state = "Solo";
		snap.steamStatus = "Solo";
	}

	snap.hash = HashSnapshot(snap);
	return snap;
}

void PushSteam(const PresenceSnapshot& snap)
{
	if (!g_steamRuntimeEnabled) {
		return;
	}
	CMP_SteamSetRichPresence("status", snap.steamStatus.c_str());
	if (snap.phase == PresencePhase::Host || snap.phase == PresencePhase::Guest) {
		CMP_SteamSetRichPresence("steam_player_group", std::to_string(snap.playerCount).c_str());
	} else {
		CMP_SteamSetRichPresence("steam_player_group", "");
	}
}

void PushSnapshot(const PresenceSnapshot& snap, bool force)
{
	const double now = NowSec();
	const bool unchanged = snap.hash == g_lastPushed.hash;
	const bool aggressive = g_discordConnected && now < g_discordRepushUntilSec;
	const double heartbeatInterval = aggressive ? 5.0 : 30.0;
	if (!force && unchanged && (now - g_lastPushSec) < 1.0) {
		return;
	}
	if (!force && unchanged && (now - g_lastHeartbeatSec) < heartbeatInterval) {
		return;
	}

	if (snap.phase == PresencePhase::Host || snap.phase == PresencePhase::Guest) {
		if (g_sessionStart == 0) {
			g_sessionStart = NowUnix();
		}
	} else {
		g_sessionStart = 0;
	}

	PushDiscord(snap);
	PushSteam(snap);
	g_lastPushed = snap;
	g_lastPushSec = now;
	g_lastHeartbeatSec = now;
}

void TryProbeSteam()
{
	auto& s = CMP_Session();
	if (!s.settings.presenceSteam || g_steamRuntimeEnabled || g_steamProbeDone) {
		return;
	}
	g_steamProbeDone = true;
	const auto probe = CMP_ProbeSteamApi();
	g_steamProbeNote = CMP_FormatSteamProbe(probe);
	g_steamRuntimeEnabled = CMP_SteamPresenceAvailable();
	if (!g_steamRuntimeEnabled) {
		REX::WARN("{}", g_steamProbeNote);
	} else {
		REX::INFO("Steam presence enabled: {}", g_steamProbeNote);
	}
}

}  // namespace cmp_presence
