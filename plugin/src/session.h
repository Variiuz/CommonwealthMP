#pragma once

#include "cmp_blobs.hpp"
#include "cmp_protocol.hpp"
#include "net/motion_interp.h"

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
	int interpDelayMs{ -1 }; // <0 = auto from PoseHz (~1.5 ticks)
	int reliableRetries{ 8 };
	int blobAssembleTtlMs{ 5000 };
	bool pointerHud{ true };
	int pointerSeconds{ 4 };
	std::string playerKey;
	std::string playerName{ "Player" };
	std::string password;
	std::string serverExe;

	bool overlayVisible{ false };
	bool overlayChatOpen{ true };
	bool overlayDebugOpen{ true };
	std::uint32_t overlayToggleKey{ 0x2D };
	float overlayFontScale{ 1.0f };

	bool presenceDiscord{ true };
	bool presenceSteam{ false };
};

struct QueuedPose {
	cmp::PlayerPose pose{};
	double recvSec{ 0.0 };
};

struct NetRuntime {
	bool joined{ false };
	std::uint32_t myPeerId{ 0 };
	bool isHost{ false };
	bool isNewPlayer{ false };
	std::vector<QueuedPose> incoming;
	std::unordered_map<std::uint32_t, cmp::PlayerPose> latestPose;
	std::unordered_map<std::uint32_t, cmp_motion::PoseRing> poseRing;
	std::unordered_map<std::uint32_t, cmp_motion::PoseRing> actorRing;
	std::unordered_map<std::uint32_t, cmp::ActorPose> latestActors;
	cmp::WorldSnapshot lastSnapshot{};
	bool haveSnapshot{ false };
	bool snapshotApplied{ false };
	bool probedForms{ false };
	double lastSend{ 0.0 };
	double lastPointerHud{ 0.0 };
	double lastSendPoseSec{ 0.0 };
	double lastRecvPoseSec{ 0.0 };
	float measuredRttMs{ 0.0f };
	double joinSentSec{ 0.0 };
	double lastHelloRetrySec{ 0.0 };
	std::uint32_t udpToken{ 0 };
	bool udpBound{ false };
};

struct GhostRuntime {
	std::unordered_map<std::uint32_t, RE::ObjectRefHandle> byPeer;
	std::unordered_map<std::uint32_t, std::string> names;
	std::uint32_t animOverridePeer{ 0 };
	std::uint32_t animOverrideFlags{ 0 };
	double animOverrideUntil{ 0 };
};

struct BlobRuntime {
	std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> appearances;
	std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> inventories;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> appearanceParts;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> inventoryParts;
	std::uint64_t lastEquipKey{ 0 };
	std::uint64_t lastInvKey{ 0 };
	double lastAppearanceSend{ 0.0 };
	double lastInventorySend{ 0.0 };
	bool pendingInventoryForce{ false };
	double pendingInventoryAt{ 0.0 };
};

struct OverlayRuntime {
	std::mutex mutex;
	std::string status;
	std::string pointer;
	std::vector<std::pair<std::uint32_t, std::string>> peers;
	std::mutex chatMutex;
	std::vector<std::string> chatHistory;
};

struct PresenceRuntime {
	std::string serverName;
	std::uint32_t maxPlayers{ 0 };
	bool interior{ false };
	std::uint32_t worldspace{ 0 };
};

struct MenuJoinRuntime {
	bool menuJoin{ false };
	std::uint8_t joinFlags{ 0 };
	std::uint32_t sessionFlags{ 0 };
};

struct Session {
	Settings settings;
	NetRuntime net;
	GhostRuntime ghosts;
	BlobRuntime blobs;
	OverlayRuntime overlay;
	PresenceRuntime presence;
	MenuJoinRuntime menu;
	std::mutex mutex;
	std::string lastStatus{ "idle" };
	std::string lastGhostNote;
	std::string lastReject;
};

struct SessionQueryResult {
	bool ok{ false };
	cmp::SessionInfo info{};
	std::string error;
};

Session& CMP_Session();
