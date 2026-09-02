#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cmp_blobs.hpp"
#include "cmp_net.hpp"
#include "cmp_protocol.hpp"
#include "cmp_udp.hpp"
#include "cmp_util.hpp"
#include "config.hpp"
#include "sim.hpp"

constexpr int kMaxDatagram = 512;
constexpr double kClientTimeoutSec = 8.0;
constexpr double kPendingTcpTimeoutSec = 10.0;
constexpr double kPersistIntervalSec = 2.0;
constexpr double kStatusBarIntervalSec = 1.0;
constexpr double kStatusLogIntervalSec = 30.0;
constexpr const char* kServerVersion = "0.7.0";  // x-release-please-version

namespace fs = std::filesystem;

struct PlayerRec {
	std::string key;
	std::string name;
	bool havePose = false;
	std::uint32_t locationFormId = cmp::kCommonwealthWorldspace;
	float x = cmp::kSanctuaryX;
	float y = cmp::kSanctuaryY;
	float z = cmp::kSanctuaryZ;
	float yaw = 0.0f;
	std::vector<std::uint8_t> appearance;
	std::vector<std::uint8_t> inventory;
};

struct Client {
	std::uint64_t connId = 0;
	sockaddr_in tcpAddr{};
	sockaddr_in udpAddr{};
	bool udpBound = false;
	std::uint32_t udpToken = 0;
	std::uint32_t peerId = 0;
	std::string playerKey;
	std::string name;
	double lastSeen = 0.0;
	std::uint64_t posesIn = 0;
	cmp::PlayerPose lastPose{};
	bool havePose = false;
	std::vector<std::uint8_t> appearance;
	std::vector<std::uint8_t> inventory;
};

struct PendingTcp {
	std::uint64_t connId = 0;
	sockaddr_in addr{};
	double connectedAt = 0.0;
};

struct SessionWorld {
	bool created = false;
	std::uint32_t spawnLocation = cmp::kCommonwealthWorldspace;
	float spawnX = cmp::kSanctuaryX;
	float spawnY = cmp::kSanctuaryY;
	float spawnZ = cmp::kSanctuaryZ;
	float gameHour = 10.0f;
	float gameDaysPassed = 0.0f;
	std::uint32_t weatherFormId = 0;
	std::uint32_t hostPeerId = 0;
	std::uint32_t hostLocation = cmp::kCommonwealthWorldspace;
	float hostX = cmp::kSanctuaryX;
	float hostY = cmp::kSanctuaryY;
	float hostZ = cmp::kSanctuaryZ;
};

struct ProcStats {
	double cpuPercent = 0.0;
	double memMb = 0.0;
#ifdef _WIN32
	ULARGE_INTEGER lastSys{};
	ULARGE_INTEGER lastProc{};
	bool haveSample = false;
#endif
};

extern std::atomic<bool> g_running;
extern fs::path g_sessionDir;

std::string addr_key(const sockaddr_in& a);
bool same_addr(const sockaddr_in& a, const sockaddr_in& b);
double now_sec();
std::string exe_dir();
fs::path session_dir();
void set_session_dir(const fs::path& path);
void send_to(CmpSocket sock, const sockaddr_in& dest, const void* data, int len, const char* what);
void send_blob(CmpSocket sock, const sockaddr_in& dest, cmp::Msg type, std::uint32_t peerId, const std::vector<std::uint8_t>& blob);
void persist_world(const SessionWorld& world);
void persist_player(const PlayerRec& rec);
void load_world(SessionWorld& world, std::unordered_map<std::string, PlayerRec>& players);
void flush_dirty(SessionWorld& world, bool& worldDirty, std::unordered_map<std::string, PlayerRec>& players, std::unordered_set<std::string>& dirtyPlayers);
void print_usage();
void print_banner();
void backup_session_folder();
void log_json_event(bool enabled, const std::string& json);
std::uint32_t build_session_flags(const ServerConfig& cfg);
void load_bans(std::unordered_set<std::string>& bans);
void persist_bans(const std::unordered_set<std::string>& bans);
bool in_interest(const Client& a, const Client& b, float maxUu);
void sample_proc_stats(ProcStats& stats);
Client* find_host(std::unordered_map<std::uint32_t, Client>& clients, SessionWorld& world);
bool take_blob(cmp::BlobAssembly& assembly, const char* buf, int n, std::vector<std::uint8_t>& out);
std::string lower_copy(std::string s);
std::vector<std::string> split_words(const std::string& line);
std::uint32_t alloc_peer(std::uint32_t& nextPeer);
