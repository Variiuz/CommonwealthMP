#pragma once

#include <cstdint>
#include <string>

#ifndef CMP_DISCORD_APP_ID
#define CMP_DISCORD_APP_ID "0"
#endif
#ifndef CMP_DISCORD_IMAGE_KEY
#define CMP_DISCORD_IMAGE_KEY "cmp_logo1"
#endif

namespace cmp_presence {

enum class PresencePhase : std::uint8_t {
	Idle,
	MenuJoin,
	Connecting,
	Host,
	Guest
};

struct PresenceSnapshot {
	PresencePhase phase{ PresencePhase::Idle };
	std::string details;
	std::string state;
	std::string steamStatus;
	std::uint32_t playerCount{ 0 };
	std::uint32_t maxPlayers{ 0 };
	std::uint64_t hash{ 0 };
};

extern bool g_discordInitialized;
extern bool g_discordConnected;
extern bool g_discordWanted;
extern double g_discordLastInitSec;
extern double g_discordLastPushSec;
extern std::string g_discordError;
extern std::string g_discordUser;
extern char g_discordDetails[128];
extern char g_discordState[128];

extern bool g_steamRuntimeEnabled;
extern bool g_steamProbeDone;
extern double g_lastPushSec;
extern double g_lastHeartbeatSec;
extern std::int64_t g_sessionStart;
extern PresenceSnapshot g_lastPushed;
extern std::string g_steamProbeNote;

extern double g_discordRepushUntilSec;
extern bool g_discordPipeReachable;
extern bool g_discordProcessElevated;

double NowSec();
std::int64_t NowUnix();
const char* DiscordAppId();
bool DiscordAppIdValid();

void RefreshDiscordDiagnostics();
void ShutdownDiscord();
bool InitDiscord(bool force);
void PushDiscord(const PresenceSnapshot& snap);
void TickDiscordConnection();

std::uint64_t HashSnapshot(const PresenceSnapshot& snap);
std::string LocationText(bool joined, bool interior, std::uint32_t worldspace);
PresenceSnapshot BuildSnapshot();
void PushSteam(const PresenceSnapshot& snap);
void PushSnapshot(const PresenceSnapshot& snap, bool force);
void TryProbeSteam();

}  // namespace cmp_presence
