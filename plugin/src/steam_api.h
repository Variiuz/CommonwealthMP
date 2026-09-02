#pragma once

#include <cstdint>
#include <string>

// Steam API helpers (FO4 already initialized steam_api64.dll; no SteamAPI_Init here).

std::string CMP_SteamPersonaName();
std::string CMP_ProbeSteamLobby();

struct SteamApiProbe {
	bool dllLoaded{ false };
	std::string dllVersion;
	bool createInterface{ false };
	std::int32_t steamUser{ 0 };
	std::int32_t steamPipe{ 0 };
	std::string friendsVersion;
	std::string personaName;
	bool richPresenceProbe{ false };
	std::string error;
};

SteamApiProbe CMP_ProbeSteamApi();
bool CMP_SteamPresenceAvailable();
bool CMP_SteamSetRichPresence(const char* key, const char* value);
void CMP_SteamClearRichPresence();
std::string CMP_FormatSteamProbe(const SteamApiProbe& probe);
