#include "pch.h"
#include "presence/internal.h"
#include "presence.h"
#include "session.h"
#include "steam_api.h"

#include <sstream>
#include <string>

namespace cmp_presence {

bool g_discordInitialized{ false };
bool g_discordConnected{ false };
bool g_discordWanted{ false };
double g_discordLastInitSec{ 0.0 };
double g_discordLastPushSec{ 0.0 };
std::string g_discordError;
std::string g_discordUser;
char g_discordDetails[128]{};
char g_discordState[128]{};

bool g_steamRuntimeEnabled{ false };
bool g_steamProbeDone{ false };
double g_lastPushSec{ 0.0 };
double g_lastHeartbeatSec{ 0.0 };
std::int64_t g_sessionStart{ 0 };
PresenceSnapshot g_lastPushed{};
std::string g_steamProbeNote;

double g_discordRepushUntilSec{ 0.0 };
bool g_discordPipeReachable{ false };
bool g_discordProcessElevated{ false };

}  // namespace cmp_presence

void CMP_Presence_Init()
{
	using namespace cmp_presence;
	auto& s = CMP_Session();
	g_steamRuntimeEnabled = false;
	g_steamProbeDone = false;
	g_steamProbeNote.clear();
	g_discordWanted = s.settings.presenceDiscord;

	if (!s.settings.presenceSteam) {
		g_steamProbeNote = "cmp_steam: disabled in INI";
	}

	if (!g_discordWanted) {
		REX::INFO("Discord presence disabled in INI");
		return;
	}
	if (!DiscordAppIdValid()) {
		g_discordError = "CMP_DISCORD_APP_ID missing at build time";
		REX::WARN("Discord presence disabled: {}", g_discordError);
		return;
	}
	REX::INFO("Discord presence enabled appId={} image={}", DiscordAppId(), CMP_DISCORD_IMAGE_KEY);
}

void CMP_Presence_OnPreLoad()
{
	using namespace cmp_presence;
	// Keep the Discord IPC session alive across save loads. Shutting down here lets
	// Discord fall back to Fallout 4's registered-game presence until we reconnect.
	g_lastPushed = {};
	g_sessionStart = 0;
	g_discordLastPushSec = 0.0;
	g_lastHeartbeatSec = 0.0;
}

void CMP_Presence_OnGameReady()
{
	using namespace cmp_presence;
	TryProbeSteam();
	if (!g_discordWanted || !DiscordAppIdValid()) {
		return;
	}
	g_discordRepushUntilSec = NowSec() + 90.0;
	if (!g_discordInitialized || !g_discordConnected) {
		InitDiscord(true);
	}
	CMP_Presence_Invalidate();
}

void CMP_Presence_Shutdown()
{
	using namespace cmp_presence;
	ShutdownDiscord();
	if (g_steamRuntimeEnabled) {
		CMP_SteamClearRichPresence();
	}
	g_lastPushed = {};
	g_sessionStart = 0;
}

void CMP_Presence_Invalidate()
{
	using namespace cmp_presence;
	const auto snap = BuildSnapshot();
	PushSnapshot(snap, true);
}

void CMP_Presence_Tick()
{
	using namespace cmp_presence;
	if (g_discordWanted && DiscordAppIdValid()) {
		if (!g_discordInitialized || (!g_discordConnected && (NowSec() - g_discordLastInitSec) >= 15.0)) {
			InitDiscord(!g_discordInitialized);
		}
		TickDiscordConnection();
		if (g_discordInitialized && g_discordConnected && g_discordLastPushSec == 0.0) {
			CMP_Presence_Invalidate();
		}
	}

	auto& s = CMP_Session();
	if (!g_steamProbeDone && s.settings.presenceSteam) {
		TryProbeSteam();
	}

	const auto snap = BuildSnapshot();
	const double now = NowSec();
	const bool heartbeat = (now - g_lastHeartbeatSec) >= 30.0;
	PushSnapshot(snap, heartbeat);
}

bool CMP_Presence_ReinitDiscord()
{
	using namespace cmp_presence;
	if (!g_discordWanted) {
		return false;
	}
	RefreshDiscordDiagnostics();
	return InitDiscord(true);
}

std::string CMP_PresenceStatusText()
{
	using namespace cmp_presence;
	const auto snap = BuildSnapshot();
	std::ostringstream o;
	o << "cmp_presence:";
	if (!g_discordWanted) {
		o << " discord=ini_off";
	} else if (!DiscordAppIdValid()) {
		o << " discord=no_app_id";
	} else if (g_discordConnected) {
		o << " discord=connected";
	} else if (g_discordInitialized) {
		o << " discord=waiting";
	} else {
		o << " discord=off";
	}
	o << " appId=" << DiscordAppId();
	o << " image=" << CMP_DISCORD_IMAGE_KEY;
	o << " pipe=" << (g_discordPipeReachable ? "yes" : "no");
	if (g_discordProcessElevated) {
		o << " elevated=yes";
	}
	if (!g_discordUser.empty()) {
		o << " user=" << g_discordUser;
	}
	if (!g_discordError.empty()) {
		o << " discord_err=\"" << g_discordError << "\"";
	}
	o << " steam=" << (g_steamRuntimeEnabled ? "on" : "off");
	if (!g_steamProbeNote.empty()) {
		o << " (" << g_steamProbeNote << ")";
	}
	o << " phase=";
	switch (snap.phase) {
	case PresencePhase::Idle:
		o << "idle";
		break;
	case PresencePhase::MenuJoin:
		o << "menu";
		break;
	case PresencePhase::Connecting:
		o << "connecting";
		break;
	case PresencePhase::Host:
		o << "host";
		break;
	case PresencePhase::Guest:
		o << "guest";
		break;
	}
	o << " details=\"" << snap.details << "\" state=\"" << snap.state << "\"";
	o << " players=" << snap.playerCount;
	if (snap.maxPlayers > 0) {
		o << "/" << snap.maxPlayers;
	}
	return o.str();
}
