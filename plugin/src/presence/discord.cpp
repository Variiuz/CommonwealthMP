#include "pch.h"
#include "presence/internal.h"
#include "presence.h"
#include "cmp_util.hpp"

#pragma pack(push, 8)
#include <discord.h>
#pragma pack(pop)

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// F4SE loads plugins from Data/F4SE/Plugins; the OS only searches beside Fallout4.exe
// for hard imports. Delay-load so LoadDiscordSdkBesidePlugin() can LoadLibrary the
// side-by-side discord_game_sdk.dll before the first DiscordCreate call.
#pragma comment(lib, "delayimp.lib")
#pragma comment(linker, "/DELAYLOAD:discord_game_sdk.dll")

namespace cmp_presence {

namespace {

discord::Core* g_discordCore{ nullptr };
HMODULE g_discordSdkModule{ nullptr };

bool LoadDiscordSdkBesidePlugin()
{
	if (g_discordSdkModule) {
		return true;
	}

	HMODULE self = nullptr;
	if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&LoadDiscordSdkBesidePlugin),
			&self) ||
		!self) {
		g_discordError = "GetModuleHandleEx failed for CommonwealthMP.dll";
		return false;
	}

	wchar_t modulePath[MAX_PATH]{};
	const DWORD n = GetModuleFileNameW(self, modulePath, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) {
		g_discordError = "GetModuleFileNameW failed for CommonwealthMP.dll";
		return false;
	}

	wchar_t* slash = wcsrchr(modulePath, L'\\');
	if (!slash) {
		g_discordError = "plugin path has no directory";
		return false;
	}
	slash[1] = L'\0';
	wcsncat_s(modulePath, L"discord_game_sdk.dll", _TRUNCATE);

	g_discordSdkModule = LoadLibraryW(modulePath);
	if (!g_discordSdkModule) {
		g_discordError = "LoadLibrary discord_game_sdk.dll failed (ship it next to CommonwealthMP.dll)";
		return false;
	}
	return true;
}

std::uint64_t DiscordAppIdU64()
{
	const char* appId = DiscordAppId();
	if (!appId || !appId[0]) {
		return 0;
	}
	char* end = nullptr;
	const unsigned long long value = std::strtoull(appId, &end, 10);
	if (!end || end == appId || *end != '\0') {
		return 0;
	}
	return static_cast<std::uint64_t>(value);
}

void PollDiscordUser()
{
	if (!g_discordCore) {
		return;
	}

	discord::User user{};
	const discord::Result result = g_discordCore->UserManager().GetCurrentUser(&user);
	if (result == discord::Result::Ok) {
		const char* username = user.GetUsername();
		std::string name = username ? username : "";
		if (!g_discordConnected || g_discordUser != name) {
			g_discordConnected = true;
			g_discordUser = std::move(name);
			g_discordError.clear();
			REX::INFO("Discord Game SDK connected as {}", g_discordUser.empty() ? "?" : g_discordUser);
			CMP_Presence_Invalidate();
		}
		return;
	}

	if (g_discordConnected) {
		g_discordConnected = false;
		g_discordUser.clear();
		g_discordError = "disconnected result=" + std::to_string(static_cast<int>(result));
		REX::WARN("Discord Game SDK {}", g_discordError);
	}
}

}  // namespace

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
		           g_discordError == "connecting to Discord" ||
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
	return DiscordAppIdU64() != 0;
}

void ShutdownDiscord()
{
	if (!g_discordInitialized && !g_discordCore) {
		return;
	}
	if (g_discordCore) {
		g_discordCore->ActivityManager().ClearActivity([](discord::Result) {});
		g_discordCore->RunCallbacks();
		delete g_discordCore;
		g_discordCore = nullptr;
	}
	g_discordInitialized = false;
	g_discordConnected = false;
	g_discordUser.clear();
}

bool InitDiscord(bool force)
{
	if (!g_discordWanted) {
		return false;
	}
	const std::uint64_t appId = DiscordAppIdU64();
	if (appId == 0) {
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

	if (!LoadDiscordSdkBesidePlugin()) {
		REX::WARN("Discord Game SDK {}", g_discordError);
		return false;
	}

	discord::Core* core = nullptr;
	const discord::Result createResult =
		discord::Core::Create(appId, DiscordCreateFlags_NoRequireDiscord, &core);
	if (createResult != discord::Result::Ok || !core) {
		g_discordError = "Core::Create failed result=" + std::to_string(static_cast<int>(createResult));
		REX::WARN("Discord Game SDK {}", g_discordError);
		return false;
	}

	g_discordCore = core;
	g_discordInitialized = true;
	g_discordLastInitSec = now;
	g_discordConnected = false;
	g_discordUser.clear();
	RefreshDiscordDiagnostics();
	g_discordError = g_discordPipeReachable ? "connecting to Discord" : "waiting for Discord desktop";
	REX::INFO("Discord Game SDK initialize appId={} pipe={} elevated={}",
		DiscordAppId(), g_discordPipeReachable ? "yes" : "no", g_discordProcessElevated);
	PollDiscordUser();
	return g_discordConnected;
}

void PushDiscord(const PresenceSnapshot& snap)
{
	if (!g_discordCore) {
		return;
	}
	cmp::copy_cstr(g_discordDetails, sizeof(g_discordDetails), snap.details.c_str());
	cmp::copy_cstr(g_discordState, sizeof(g_discordState), snap.state.c_str());

	discord::Activity activity{};
	activity.SetDetails(g_discordDetails);
	activity.SetState(g_discordState);
	activity.GetAssets().SetLargeImage(CMP_DISCORD_IMAGE_KEY);
	activity.GetAssets().SetLargeText("CMP");
	if (snap.phase == PresencePhase::Host || snap.phase == PresencePhase::Guest) {
		if (g_sessionStart > 0) {
			activity.GetTimestamps().SetStart(g_sessionStart);
		}
		if (snap.maxPlayers > 0) {
			activity.GetParty().GetSize().SetCurrentSize(static_cast<std::int32_t>(snap.playerCount));
			activity.GetParty().GetSize().SetMaxSize(static_cast<std::int32_t>(snap.maxPlayers));
		}
	}

	g_discordCore->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
		if (result != discord::Result::Ok) {
			g_discordError = "UpdateActivity result=" + std::to_string(static_cast<int>(result));
		}
	});
	g_discordLastPushSec = NowSec();
}

void TickDiscordConnection()
{
	if (!g_discordCore) {
		return;
	}
	static double lastDiagSec = 0.0;
	const double now = NowSec();
	if (!g_discordConnected && (now - lastDiagSec) >= 3.0) {
		lastDiagSec = now;
		RefreshDiscordDiagnostics();
	}
	g_discordCore->RunCallbacks();
	PollDiscordUser();
}

}  // namespace cmp_presence
