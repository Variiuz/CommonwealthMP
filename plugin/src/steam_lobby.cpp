#include "pch.h"
#include "cmp.h"

namespace {

std::string TruncateName(const char* raw)
{
	if (!raw || !raw[0]) {
		return {};
	}
	char buf[32]{};
	cmp::copy_cstr(buf, sizeof(buf), raw);
	return buf;
}

void* SteamFriendsIface()
{
	auto steam = REX::W32::GetModuleHandleW(L"steam_api64.dll");
	if (!steam) {
		return nullptr;
	}
	using SteamFriendsFn = void*(__cdecl*)();
	if (auto fn = reinterpret_cast<SteamFriendsFn>(REX::W32::GetProcAddress(steam, "SteamFriends"))) {
		if (auto* iface = fn()) {
			return iface;
		}
	}
	using CreateInterfaceFn = void*(__cdecl*)(const char*);
	auto create = reinterpret_cast<CreateInterfaceFn>(REX::W32::GetProcAddress(steam, "SteamInternal_CreateInterface"));
	if (!create) {
		return nullptr;
	}
	const char* versions[] = {
		"SteamFriends015",
		"SteamFriends014",
		"SteamFriends017",
		"SteamFriends013"
	};
	for (const char* ver : versions) {
		if (auto* iface = create(ver)) {
			return iface;
		}
	}
	return nullptr;
}

class ISteamFriendsName {
public:
	virtual const char* GetPersonaName() = 0;
};

}  // namespace

std::string CMP_SteamPersonaName()
{
	auto* iface = SteamFriendsIface();
	if (!iface) {
		return {};
	}
	auto* friends = reinterpret_cast<ISteamFriendsName*>(iface);
	const char* name = friends->GetPersonaName();
	return TruncateName(name);
}

std::string CMP_ProbeSteamLobby()
{
	// FO4 already called SteamAPI_Init.
	auto steam = REX::W32::GetModuleHandleW(L"steam_api64.dll");
	if (!steam) {
		return "cmp_lobby: steam_api64.dll not loaded. Join IP is the session.";
	}

	std::string out = "cmp_lobby: steam_api64 loaded (FO4 already initialized Steam). ";

	using CreateInterfaceFn = void*(__cdecl*)(const char*);
	auto create = reinterpret_cast<CreateInterfaceFn>(REX::W32::GetProcAddress(steam, "SteamInternal_CreateInterface"));
	if (!create) {
		out += "SteamInternal_CreateInterface missing (2015 FO4 DLL is old). Join IP stays the real path.";
		return out;
	}

	const char* versions[] = {
		"SteamMatchMaking009",
		"SteamMatchMaking008",
		"SteamMatchMaking007",
		"SteamMatchMaking006",
		"SteamMatchMaking010",
		"SteamMatchMaking013"
	};

	const char* hit = nullptr;
	void* mm = nullptr;
	for (const char* ver : versions) {
		mm = create(ver);
		if (mm) {
			hit = ver;
			break;
		}
	}

	if (!mm) {
		out += "No ISteamMatchmaking interface. CreateLobby likely AccessDenied or unavailable. Use Join IP / Tailscale.";
		REX::INFO("{}", out);
		return out;
	}

	out += "Found ";
	out += hit;
	out += ". CreateLobby is NOT invoked from this probe (FO4 2015 vtable is unproven; FO4 is not a Steam MP title). ";
	out += "If you later call CreateLobby, log LobbyCreated_t.m_eResult. On OK: SetLobbyGameServer(ip, port, nil). ";
	out += "Gameplay stays on UDP to CommonwealthMP.Server.exe.";
	REX::INFO("{}", out);
	return out;
}
