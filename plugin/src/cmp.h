#pragma once

#include "cmp_blobs.hpp"
#include "cmp_protocol.hpp"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Settings {
	std::string host{ "127.0.0.1" };
	std::uint16_t port{ cmp::kDefaultPort };
	bool autoJoin{ false };
	std::string ghostEditorId{ "CMP_RemotePlayer" };
	bool ghostSpawn{ true };
	std::uint32_t ghostSourceForm{ 0x0001D323 };
	int poseHz{ 20 };
	bool pointerHud{ true };
	int pointerSeconds{ 4 };
	std::string playerKey;
	std::string playerName{ "fo4" };
};

struct QueuedPose {
	cmp::PlayerPose pose{};
};

struct Session {
	Settings settings;
	bool joined{ false };
	std::uint32_t myPeerId{ 0 };
	std::uint32_t fakePeerId{ 0 };
	bool isHost{ false };
	bool isNewPlayer{ false };
	std::mutex mutex;
	std::vector<QueuedPose> incoming;
	std::unordered_map<std::uint32_t, cmp::PlayerPose> latestPose;
	std::unordered_map<std::uint32_t, RE::ObjectRefHandle> ghosts;
	std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> appearances;
	std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> inventories;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> appearanceParts;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> inventoryParts;
	std::unordered_map<std::uint32_t, std::string> ghostNames;
	std::string lastStatus{ "idle" };
	std::string lastGhostNote;
	std::string lastReject;
	cmp::WorldSnapshot lastSnapshot{};
	bool haveSnapshot{ false };
	bool snapshotApplied{ false };
	bool probedForms{ false };
	double lastSend{ 0.0 };
	double lastPointerHud{ 0.0 };
	double lastAppearanceSend{ 0.0 };
	double lastInventorySend{ 0.0 };
	std::uint64_t lastEquipKey{ 0 };
	std::uint64_t lastInvKey{ 0 };
	bool menuJoin{ false };
	std::uint8_t joinFlags{ 0 };
};

struct SessionQueryResult {
	bool ok{ false };
	cmp::SessionInfo info{};
	std::string error;
};

Session& CMP_Session();

void CMP_LoadSettings();
void CMP_SaveNetworkSettings(const std::string& host, std::uint16_t port);
bool CMP_Join(std::string host, std::uint16_t port, std::uint8_t flags = 0);
void CMP_Leave();
void CMP_QueryStart(std::string host, std::uint16_t port);
bool CMP_QueryPoll(SessionQueryResult& out);
void CMP_NetPoll();
void CMP_SendLocalPose();
void CMP_SendAppearance(bool force);
void CMP_SendInventory(bool force);
void CMP_ApplyGhosts();
void CMP_DespawnGhosts();
void CMP_ApplyGhostAppearance(RE::Actor* actor, std::uint32_t peerId);
void CMP_ApplyGhostInventory(RE::Actor* actor, std::uint32_t peerId);
void CMP_FillLocalMotion(cmp::PlayerPose& pose);
void CMP_ApplyGhostPuppet(RE::Actor* actor, const cmp::PlayerPose& pose);
void CMP_ResetGhostPuppet(std::uint32_t peerId);
void CMP_ResetAllPuppets();
bool CMP_IsPaintableGhostBase(RE::TESNPC* npc);
void CMP_SetGhostLabel(RE::Actor* actor, const char* name);
std::string CMP_SteamPersonaName();
void CMP_RegisterConsole();
void CMP_Print(const std::string& line);
std::string CMP_StatusText();
int CMP_CountGhostsWith3D();
std::string CMP_PointerText();
void CMP_PointerTick();
bool CMP_GotoNearest();
std::string CMP_ProbeSteamLobby();
void CMP_ProbeForms();
std::string CMP_DumpLive();
void CMP_InstallCrashHandler();
void CMP_WatchQuit();
void CMP_CrashNote(const char* what);
bool CMP_SehCall(const char* what, void (*fn)(void*), void* ctx);
void CMP_ApplyWorldSnapshot(const cmp::WorldSnapshot& snap);
void CMP_OnGameReady();
void CMP_OnPreLoad();
void CMP_InstallMenu();
void CMP_MenuTick();
void CMP_MenuOnNewGame();
bool CMP_MenuJoinPending();
std::string CMP_VersionStamp();
bool CMP_PlayerInCommonwealth();
void CMP_EnsureCommonwealthExterior();
void CMP_StripLocalGear();
RE::TESForm* CMP_ResolveForm(std::uint32_t rawId, const char* plugin);
