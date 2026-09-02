#include "pch.h"
#include "steam_api.h"
#include "steam/internal.h"
#include "crash.h"

#include <cstring>
#include <cstdlib>

namespace cmp_steam {

ISteamFriends014* FriendsPresenceIface()
{
	if (!g_steamPresenceOk || !g_friendsMatch.iface) {
		return nullptr;
	}
	return reinterpret_cast<ISteamFriends014*>(g_friendsMatch.iface);
}

bool RichPresenceVersionOk(const char* version)
{
	if (!version) {
		return false;
	}
	// Bare SteamFriends() export has no version tag; vtable layout is unproven on FO4.
	if (std::strcmp(version, "SteamFriends()") == 0) {
		return false;
	}
	const char* digits = nullptr;
	if (std::strncmp(version, "SteamFriends", 12) == 0) {
		digits = version + 12;
	} else if (std::strncmp(version, "SteamAPI_SteamFriends_v", 23) == 0) {
		digits = version + 23;
	}
	if (!digits || !digits[0]) {
		return false;
	}
	const int num = std::atoi(digits);
	return num >= 14;
}

void RichPresenceSetFn(void* raw)
{
	auto* ctx = static_cast<RichPresenceSetCtx*>(raw);
	if (!ctx || !ctx->friends || !ctx->key) {
		return;
	}
	ctx->ok = ctx->friends->SetRichPresence(ctx->key, ctx->value ? ctx->value : "");
}

void RichPresenceClearFn(void* raw)
{
	auto* friends = static_cast<ISteamFriends014*>(raw);
	if (friends) {
		friends->ClearRichPresence();
	}
}

}  // namespace cmp_steam

using cmp_steam::FriendsPresenceIface;
using cmp_steam::g_steamPresenceOk;
using cmp_steam::RichPresenceClearFn;
using cmp_steam::RichPresenceSetCtx;
using cmp_steam::RichPresenceSetFn;

bool CMP_SteamPresenceAvailable()
{
	return g_steamPresenceOk;
}

bool CMP_SteamSetRichPresence(const char* key, const char* value)
{
	auto* friends = FriendsPresenceIface();
	if (!friends || !key) {
		return false;
	}
	RichPresenceSetCtx setCtx{ friends, key, value };
	if (!CMP_SehCall("steam_set_rich", RichPresenceSetFn, &setCtx)) {
		REX::WARN("SetRichPresence crashed; disabling Steam presence");
		g_steamPresenceOk = false;
		return false;
	}
	if (!setCtx.ok) {
		return false;
	}
	return true;
}

void CMP_SteamClearRichPresence()
{
	auto* friends = FriendsPresenceIface();
	if (!friends) {
		return;
	}
	if (!CMP_SehCall("steam_clear_rich", RichPresenceClearFn, friends)) {
		REX::WARN("ClearRichPresence crashed; disabling Steam presence");
		g_steamPresenceOk = false;
	}
}
