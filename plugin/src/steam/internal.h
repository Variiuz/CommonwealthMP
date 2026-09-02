#pragma once

#include "REX/W32/BASE.h"

#include <cstdint>
#include <string>

namespace cmp_steam {

std::string TruncateName(const char* raw);

using CreateInterfaceFn = void*(__cdecl*)(const char*);
using VoidPtrFn = void*(__cdecl*)();
using GetSteamUserFn = std::int32_t(__cdecl*)();
using GetSteamPipeFn = std::int32_t(__cdecl*)();
using FindUserInterfaceFn = void*(__cdecl*)(std::int32_t, const char*);
using GetISteamFriendsFlatFn = void*(__cdecl*)(void*, std::int32_t, std::int32_t, const char*);

REX::W32::HMODULE SteamModule();
void* SteamExport(const char* name);
CreateInterfaceFn SteamCreateInterface();
std::int32_t SteamUserHandle();
std::int32_t SteamPipeHandle();
void* SteamClientPtr();

struct SteamFriendsMatch {
	void* iface{ nullptr };
	const char* version{ nullptr };
};

bool AssignFriendsMatch(SteamFriendsMatch& out, void* iface, const char* version);
SteamFriendsMatch MatchSteamFriends();
std::string ModuleFileVersion(const wchar_t* moduleName);

// ISteamFriends014 layout through ClearRichPresence (SDK order).
using SteamId = std::uint64_t;

enum class PersonaState : int {
	Offline = 0,
	Online = 1,
	Busy = 2,
	Away = 3,
	Snooze = 4,
	LookingToTrade = 5,
	LookingToPlay = 6,
};

struct FriendGameInfo {
	SteamId gameId{};
	std::uint32_t ip{};
	std::uint16_t port{};
	std::uint16_t queryPort{};
	SteamId lobby{};
};

class ISteamFriends014 {
public:
	virtual const char* GetPersonaName() = 0;
	virtual void SetPersonaName(const char* pchPersonaName) = 0;
	virtual PersonaState GetPersonaState() = 0;
	virtual int GetFriendCount(int iFriendFlags) = 0;
	virtual SteamId GetFriendByIndex(int iFriend, int iFriendFlags) = 0;
	virtual int GetFriendRelationship(SteamId steamIDFriend) = 0;
	virtual const char* GetFriendPersonaName(SteamId steamIDFriend) = 0;
	virtual PersonaState GetFriendPersonaState(SteamId steamIDFriend) = 0;
	virtual bool GetFriendGamePlayed(SteamId steamIDFriend, FriendGameInfo* pFriendGameInfo) = 0;
	virtual const char* GetFriendPersonaNameHistory(SteamId steamIDFriend, int iPersonaName) = 0;
	virtual int GetFriendSteamLevel(SteamId steamIDFriend) = 0;
	virtual const char* GetPlayerNickname(SteamId steamIDPlayer) = 0;
	virtual int GetFriendsGroupCount() = 0;
	virtual std::int16_t GetFriendsGroupIDByIndex(int iFG) = 0;
	virtual const char* GetFriendsGroupName(std::int16_t friendsGroupID) = 0;
	virtual int GetFriendsGroupMembersCount(std::int16_t friendsGroupID) = 0;
	virtual void GetFriendsGroupMembersList(std::int16_t friendsGroupID, SteamId* pOutSteamIDMembers, int nMembersCount) = 0;
	virtual bool HasFriend(SteamId steamIDFriend, int iFriendFlags) = 0;
	virtual int GetClanCount() = 0;
	virtual SteamId GetClanByIndex(int iClan) = 0;
	virtual const char* GetClanName(SteamId steamIDClan) = 0;
	virtual const char* GetClanTag(SteamId steamIDClan) = 0;
	virtual bool GetClanActivityCounts(SteamId steamIDClan, int* pnOnline, int* pnInGame, int* pnChatting) = 0;
	virtual std::uint64_t DownloadClanActivityCounts(SteamId* psteamIDClans, int cClansToDownload) = 0;
	virtual int GetFriendCountFromSource(SteamId steamIDSource) = 0;
	virtual SteamId GetFriendFromSourceByIndex(SteamId steamIDSource, int iFriend) = 0;
	virtual bool IsUserInSource(SteamId steamIDUser, SteamId steamIDSource) = 0;
	virtual void SetInGameVoiceSpeaking(SteamId steamIDUser, bool bSpeaking) = 0;
	virtual void ActivateGameOverlay(const char* pchDialog) = 0;
	virtual void ActivateGameOverlayToUser(const char* pchDialog, SteamId steamID) = 0;
	virtual void ActivateGameOverlayToWebPage(const char* pchURL) = 0;
	virtual bool SetRichPresence(const char* pchKey, const char* pchValue) = 0;
	virtual void ClearRichPresence() = 0;
};

extern SteamFriendsMatch g_friendsMatch;
extern bool g_steamPresenceOk;
extern bool g_steamPresenceProbed;

ISteamFriends014* FriendsPresenceIface();
bool RichPresenceVersionOk(const char* version);

struct PersonaProbeCtx {
	ISteamFriends014* friends{ nullptr };
	char name[32]{};
	bool ok{ false };
};

void PersonaProbeFn(void* raw);

struct RichPresenceProbeCtx {
	ISteamFriends014* friends{ nullptr };
	bool setOk{ false };
};

void RichPresenceProbeFn(void* raw);

struct RichPresenceSetCtx {
	ISteamFriends014* friends{ nullptr };
	const char* key{ nullptr };
	const char* value{ nullptr };
	bool ok{ false };
};

void RichPresenceSetFn(void* raw);
void RichPresenceClearFn(void* raw);

}  // namespace cmp_steam
