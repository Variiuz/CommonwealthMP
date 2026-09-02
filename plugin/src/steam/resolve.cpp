#include "pch.h"
#include "steam/internal.h"
#include "cmp_util.hpp"

#include "REX/W32/VERSION.h"

#include "REL/Version.h"

#include <cstdio>

namespace cmp_steam {

SteamFriendsMatch g_friendsMatch{};
bool g_steamPresenceOk{ false };
bool g_steamPresenceProbed{ false };

std::string TruncateName(const char* raw)
{
	if (!raw || !raw[0]) {
		return {};
	}
	char buf[32]{};
	cmp::copy_cstr(buf, sizeof(buf), raw);
	return buf;
}

REX::W32::HMODULE SteamModule()
{
	return REX::W32::GetModuleHandleW(L"steam_api64.dll");
}

void* SteamExport(const char* name)
{
	const auto steam = SteamModule();
	if (!steam) {
		return nullptr;
	}
	return REX::W32::GetProcAddress(steam, name);
}

CreateInterfaceFn SteamCreateInterface()
{
	return reinterpret_cast<CreateInterfaceFn>(SteamExport("SteamInternal_CreateInterface"));
}

std::int32_t SteamUserHandle()
{
	const auto fn = reinterpret_cast<GetSteamUserFn>(SteamExport("SteamAPI_GetHSteamUser"));
	return fn ? fn() : 0;
}

std::int32_t SteamPipeHandle()
{
	const auto fn = reinterpret_cast<GetSteamPipeFn>(SteamExport("SteamAPI_GetHSteamPipe"));
	return fn ? fn() : 0;
}

void* SteamClientPtr()
{
	if (const auto fn = reinterpret_cast<VoidPtrFn>(SteamExport("SteamClient"))) {
		if (auto* client = fn()) {
			return client;
		}
	}
	const char* flat[] = {
		"SteamAPI_SteamClient_v018",
		"SteamAPI_SteamClient_v017",
		"SteamAPI_SteamClient_v016",
		"SteamAPI_SteamClient_v015",
		"SteamAPI_SteamClient_v014",
		"SteamAPI_SteamClient_v013",
		"SteamAPI_SteamClient_v012",
		"SteamAPI_SteamClient_v011",
		"SteamAPI_SteamClient_v010",
		"SteamAPI_SteamClient_v009",
		"SteamAPI_SteamClient_v008",
		"SteamAPI_SteamClient_v007",
		"SteamAPI_SteamClient_v006"
	};
	for (const char* name : flat) {
		if (const auto fn = reinterpret_cast<VoidPtrFn>(SteamExport(name))) {
			if (auto* client = fn()) {
				return client;
			}
		}
	}
	const auto create = SteamCreateInterface();
	if (!create) {
		return nullptr;
	}
	const char* versions[] = {
		"SteamClient018",
		"SteamClient017",
		"SteamClient016",
		"SteamClient015",
		"SteamClient014",
		"SteamClient013",
		"SteamClient012",
		"SteamClient011",
		"SteamClient010",
		"SteamClient009",
		"SteamClient008",
		"SteamClient007",
		"SteamClient006"
	};
	for (const char* ver : versions) {
		if (auto* client = create(ver)) {
			return client;
		}
	}
	return nullptr;
}

bool AssignFriendsMatch(SteamFriendsMatch& out, void* iface, const char* version)
{
	if (!iface || !version) {
		return false;
	}
	out.iface = iface;
	out.version = version;
	return true;
}

namespace {

const char* const kFriendsVersions[] = {
	"SteamFriends018",
	"SteamFriends017",
	"SteamFriends016",
	"SteamFriends015",
	"SteamFriends014",
	"SteamFriends013",
	"SteamFriends012",
	"SteamFriends011",
	"SteamFriends010",
	"SteamFriends009",
	"SteamFriends008"
};

}  // namespace

SteamFriendsMatch MatchSteamFriends()
{
	SteamFriendsMatch out{};
	const auto steam = SteamModule();
	if (!steam) {
		return out;
	}

	if (const auto fn = reinterpret_cast<VoidPtrFn>(REX::W32::GetProcAddress(steam, "SteamFriends"))) {
		if (AssignFriendsMatch(out, fn(), "SteamFriends()")) {
			return out;
		}
	}

	const char* flat[] = {
		"SteamAPI_SteamFriends",
		"SteamAPI_SteamFriends_v018",
		"SteamAPI_SteamFriends_v017",
		"SteamAPI_SteamFriends_v016",
		"SteamAPI_SteamFriends_v015",
		"SteamAPI_SteamFriends_v014",
		"SteamAPI_SteamFriends_v013",
		"SteamAPI_SteamFriends_v012",
		"SteamAPI_SteamFriends_v011",
		"SteamAPI_SteamFriends_v010",
		"SteamAPI_SteamFriends_v009",
		"SteamAPI_SteamFriends_v008"
	};
	for (const char* name : flat) {
		if (const auto fn = reinterpret_cast<VoidPtrFn>(REX::W32::GetProcAddress(steam, name))) {
			if (AssignFriendsMatch(out, fn(), name)) {
				return out;
			}
		}
	}

	const std::int32_t user = SteamUserHandle();
	const std::int32_t pipe = SteamPipeHandle();
	if (user != 0) {
		if (const auto findUser = reinterpret_cast<FindUserInterfaceFn>(
				REX::W32::GetProcAddress(steam, "SteamInternal_FindOrCreateUserInterface"))) {
			for (const char* ver : kFriendsVersions) {
				if (AssignFriendsMatch(out, findUser(user, ver), ver)) {
					return out;
				}
			}
		}
	}

	if (const auto create = SteamCreateInterface()) {
		for (const char* ver : kFriendsVersions) {
			if (AssignFriendsMatch(out, create(ver), ver)) {
				return out;
			}
		}
	}

	if (void* client = SteamClientPtr(); client && user != 0 && pipe != 0) {
		if (const auto getFriends = reinterpret_cast<GetISteamFriendsFlatFn>(
				REX::W32::GetProcAddress(steam, "SteamAPI_ISteamClient_GetISteamFriends"))) {
			for (const char* ver : kFriendsVersions) {
				if (AssignFriendsMatch(out, getFriends(client, user, pipe, ver), ver)) {
					return out;
				}
			}
		}
	}

	return out;
}

std::string ModuleFileVersion(const wchar_t* moduleName)
{
	wchar_t path[REX::W32::MAX_PATH]{};
	if (!REX::W32::GetModuleFileNameW(REX::W32::GetModuleHandleW(moduleName), path, REX::W32::MAX_PATH)) {
		return {};
	}
	const auto ver = REL::GetFileVersion(path);
	if (!ver) {
		return {};
	}
	char buf[64]{};
	std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (*ver)[0], (*ver)[1], (*ver)[2], (*ver)[3]);
	return buf;
}

}  // namespace cmp_steam
