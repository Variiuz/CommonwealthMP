#include "pch.h"
#include "steam_api.h"
#include "steam/internal.h"
#include "crash.h"
#include "cmp_util.hpp"

#include <sstream>
#include <string>

namespace cmp_steam {

void PersonaProbeFn(void* raw)
{
	auto* ctx = static_cast<PersonaProbeCtx*>(raw);
	if (!ctx || !ctx->friends) {
		return;
	}
	const char* rawName = ctx->friends->GetPersonaName();
	if (rawName && rawName[0]) {
		cmp::copy_cstr(ctx->name, sizeof(ctx->name), rawName);
		ctx->ok = true;
	}
}

void RichPresenceProbeFn(void* raw)
{
	auto* ctx = static_cast<RichPresenceProbeCtx*>(raw);
	if (!ctx || !ctx->friends) {
		return;
	}
	ctx->setOk = ctx->friends->SetRichPresence("status", "CMP probe");
	if (ctx->setOk) {
		ctx->friends->ClearRichPresence();
	}
}

}  // namespace cmp_steam

using cmp_steam::g_friendsMatch;
using cmp_steam::g_steamPresenceOk;
using cmp_steam::g_steamPresenceProbed;
using cmp_steam::ISteamFriends014;
using cmp_steam::MatchSteamFriends;
using cmp_steam::ModuleFileVersion;
using cmp_steam::PersonaProbeCtx;
using cmp_steam::PersonaProbeFn;
using cmp_steam::RichPresenceProbeCtx;
using cmp_steam::RichPresenceProbeFn;
using cmp_steam::RichPresenceVersionOk;
using cmp_steam::SteamCreateInterface;
using cmp_steam::SteamPipeHandle;
using cmp_steam::SteamUserHandle;
using cmp_steam::TruncateName;

std::string CMP_SteamPersonaName()
{
	const auto match = MatchSteamFriends();
	if (!match.iface) {
		return {};
	}
	auto* friends = reinterpret_cast<ISteamFriends014*>(match.iface);
	return TruncateName(friends->GetPersonaName());
}

std::string CMP_ProbeSteamLobby()
{
	auto steam = REX::W32::GetModuleHandleW(L"steam_api64.dll");
	if (!steam) {
		return "cmp_lobby: steam_api64.dll not loaded. Join IP is the session.";
	}

	std::string out = "cmp_lobby: steam_api64 loaded (FO4 already initialized Steam). ";

	auto create = SteamCreateInterface();
	if (!create) {
		out += "SteamInternal_CreateInterface missing (2015 FO4 DLL is old). Join IP stays the real path.";
		return out;
	}

	const char* versions[] = {
		"SteamMatchMaking013",
		"SteamMatchMaking010",
		"SteamMatchMaking009",
		"SteamMatchMaking008",
		"SteamMatchMaking007",
		"SteamMatchMaking006"
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

SteamApiProbe CMP_ProbeSteamApi()
{
	SteamApiProbe probe{};
	g_steamPresenceProbed = true;
	g_steamPresenceOk = false;

	probe.dllLoaded = REX::W32::GetModuleHandleW(L"steam_api64.dll") != nullptr;
	if (!probe.dllLoaded) {
		probe.error = "steam_api64.dll not loaded";
		return probe;
	}
	probe.dllVersion = ModuleFileVersion(L"steam_api64.dll");
	probe.createInterface = SteamCreateInterface() != nullptr;
	probe.steamUser = SteamUserHandle();
	probe.steamPipe = SteamPipeHandle();

	g_friendsMatch = MatchSteamFriends();
	probe.friendsVersion = g_friendsMatch.version ? g_friendsMatch.version : "none";
	if (!g_friendsMatch.iface) {
		probe.error = "ISteamFriends unavailable";
		if (probe.steamUser == 0 || probe.steamPipe == 0) {
			probe.error += " (Steam user/pipe not ready; try in main menu or in-world)";
		}
		return probe;
	}

	if (probe.steamUser == 0 || probe.steamPipe == 0) {
		probe.error = "Steam user/pipe not ready";
		return probe;
	}

	auto* friends = reinterpret_cast<ISteamFriends014*>(g_friendsMatch.iface);
	PersonaProbeCtx personaCtx{ friends };
	if (!CMP_SehCall("steam_persona_probe", PersonaProbeFn, &personaCtx)) {
		probe.error = "GetPersonaName crashed (vtable mismatch)";
		return probe;
	}
	probe.personaName = personaCtx.name;

	if (!RichPresenceVersionOk(g_friendsMatch.version)) {
		probe.error = "SteamFriends version too old for Rich Presence layout";
		return probe;
	}

	if (!probe.createInterface) {
		probe.error = "SteamInternal_CreateInterface missing";
		return probe;
	}

	RichPresenceProbeCtx probeCtx{ friends };
	if (!CMP_SehCall("steam_rich_probe", RichPresenceProbeFn, &probeCtx)) {
		probe.richPresenceProbe = false;
		probe.error = "SetRichPresence crashed (vtable mismatch)";
		return probe;
	}
	probe.richPresenceProbe = probeCtx.setOk;

	if (!probe.richPresenceProbe) {
		probe.error = "SetRichPresence returned false";
		return probe;
	}

	g_steamPresenceOk = true;
	probe.error.clear();
	REX::INFO("cmp_steam: dll={} friends={} persona={} richPresence=ok",
		probe.dllVersion, probe.friendsVersion, probe.personaName);
	return probe;
}

std::string CMP_FormatSteamProbe(const SteamApiProbe& probe)
{
	std::ostringstream o;
	o << "cmp_steam: dll=" << (probe.dllLoaded ? "yes" : "no");
	if (!probe.dllVersion.empty()) {
		o << " ver=" << probe.dllVersion;
	}
	o << " createInterface=" << (probe.createInterface ? "yes" : "no");
	o << " user=" << probe.steamUser << " pipe=" << probe.steamPipe;
	o << " friends=" << (probe.friendsVersion.empty() ? "none" : probe.friendsVersion);
	if (!probe.personaName.empty()) {
		o << " persona=" << probe.personaName;
	}
	o << " richPresence=" << (probe.richPresenceProbe ? "ok" : "fail");
	if (!probe.error.empty()) {
		o << " err=" << probe.error;
	}
	return o.str();
}
