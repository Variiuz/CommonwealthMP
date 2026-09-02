#include "pch.h"
#include "presence/internal.h"
#include "presence.h"
#include "cmp_util.hpp"

#include <discord_rpc.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace cmp_presence {

bool IsProcessElevated()
{
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return false;
	}
	TOKEN_ELEVATION elevation{};
	DWORD size = 0;
	const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE;
	CloseHandle(token);
	return ok && elevation.TokenIsElevated != 0;
}

bool ProbeDiscordPipe()
{
	wchar_t pipeName[] = L"\\\\?\\pipe\\discord-ipc-0";
	const size_t pipeDigit = (sizeof(pipeName) / sizeof(wchar_t)) - 2;
	for (wchar_t digit = L'0'; digit <= L'9'; ++digit) {
		pipeName[pipeDigit] = digit;
		const HANDLE pipe = CreateFileW(
			pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (pipe != INVALID_HANDLE_VALUE) {
			CloseHandle(pipe);
			return true;
		}
		if (GetLastError() != ERROR_FILE_NOT_FOUND) {
			break;
		}
	}
	return false;
}

void RefreshDiscordDiagnostics()
{
	g_discordPipeReachable = ProbeDiscordPipe();
	g_discordProcessElevated = IsProcessElevated();
	if (!g_discordConnected && g_discordInitialized) {
		if (!g_discordPipeReachable) {
			g_discordError = "Discord IPC pipe not found (start Discord desktop app)";
			if (g_discordProcessElevated) {
				g_discordError += "; Fallout4 is elevated - run Discord as admin or launch FO4 normally";
			}
		} else if (g_discordError == "waiting for Discord desktop" ||
		           g_discordError.find("IPC pipe not found") != std::string::npos) {
			g_discordError = "Discord pipe open; enable Settings > Activity Privacy > Display current activity";
		}
	}
}

double NowSec()
{
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

std::int64_t NowUnix()
{
	return std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch())
		.count();
}

const char* DiscordAppId()
{
	return CMP_DISCORD_APP_ID;
}

bool DiscordAppIdValid()
{
	const char* appId = DiscordAppId();
	return appId && appId[0] != '\0' && std::strcmp(appId, "0") != 0;
}

void DiscordReady(const DiscordUser* user)
{
	g_discordConnected = true;
	g_discordError.clear();
	if (user && user->username) {
		g_discordUser = user->username;
		if (user->discriminator && user->discriminator[0] != '0') {
			g_discordUser += '#';
			g_discordUser += user->discriminator;
		}
	} else {
		g_discordUser.clear();
	}
	REX::INFO("Discord RPC connected as {}", g_discordUser.empty() ? "?" : g_discordUser);
	CMP_Presence_Invalidate();
}

void DiscordDisconnected(int errorCode, const char* message)
{
	g_discordConnected = false;
	g_discordUser.clear();
	g_discordError = "disconnected " + std::to_string(errorCode);
	if (message && message[0]) {
		g_discordError += ' ';
		g_discordError += message;
	}
	REX::WARN("Discord RPC {}", g_discordError);
}

void DiscordErrored(int errorCode, const char* message)
{
	g_discordError = "error " + std::to_string(errorCode);
	if (message && message[0]) {
		g_discordError += ' ';
		g_discordError += message;
	}
	REX::WARN("Discord RPC {}", g_discordError);
}

void ShutdownDiscord()
{
	if (!g_discordInitialized) {
		return;
	}
	Discord_ClearPresence();
	Discord_Shutdown();
	g_discordInitialized = false;
	g_discordConnected = false;
	g_discordUser.clear();
}

bool InitDiscord(bool force)
{
	if (!g_discordWanted) {
		return false;
	}
	if (!DiscordAppIdValid()) {
		g_discordError = "CMP_DISCORD_APP_ID missing at build time";
		return false;
	}

	const double now = NowSec();
	if (g_discordInitialized && g_discordConnected && !force) {
		return true;
	}
	if (g_discordInitialized && !force && (now - g_discordLastInitSec) < 15.0) {
		return g_discordConnected;
	}

	if (g_discordInitialized) {
		ShutdownDiscord();
	}

	DiscordEventHandlers handlers{};
	handlers.ready = DiscordReady;
	handlers.disconnected = DiscordDisconnected;
	handlers.errored = DiscordErrored;
	Discord_Initialize(DiscordAppId(), &handlers, 0, nullptr);
	g_discordInitialized = true;
	g_discordLastInitSec = now;
	RefreshDiscordDiagnostics();
	g_discordError = g_discordPipeReachable ? "connecting to Discord" : "waiting for Discord desktop";
	REX::INFO("Discord RPC initialize appId={} pipe={} elevated={}",
		DiscordAppId(), g_discordPipeReachable ? "yes" : "no", g_discordProcessElevated);
	return false;
}

void PushDiscord(const PresenceSnapshot& snap)
{
	if (!g_discordInitialized) {
		return;
	}
	cmp::copy_cstr(g_discordDetails, sizeof(g_discordDetails), snap.details.c_str());
	cmp::copy_cstr(g_discordState, sizeof(g_discordState), snap.state.c_str());

	DiscordRichPresence presence{};
	presence.details = g_discordDetails;
	presence.state = g_discordState;
	presence.largeImageKey = CMP_DISCORD_IMAGE_KEY;
	presence.largeImageText = "CMP";
	if (snap.phase == PresencePhase::Host || snap.phase == PresencePhase::Guest) {
		if (g_sessionStart > 0) {
			presence.startTimestamp = g_sessionStart;
		}
		if (snap.maxPlayers > 0) {
			presence.partySize = static_cast<int>(snap.playerCount);
			presence.partyMax = static_cast<int>(snap.maxPlayers);
		}
	}
	Discord_UpdatePresence(&presence);
	g_discordLastPushSec = NowSec();
}

void TickDiscordConnection()
{
	if (!g_discordInitialized) {
		return;
	}
	static double lastDiagSec = 0.0;
	const double now = NowSec();
	if (!g_discordConnected && (now - lastDiagSec) >= 3.0) {
		lastDiagSec = now;
		RefreshDiscordDiagnostics();
	}
	Discord_RunCallbacks();
}

}  // namespace cmp_presence
