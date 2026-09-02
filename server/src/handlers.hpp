#pragma once

#include <string_view>

#include "server_state.hpp"

struct ServerRuntime {
	CmpSocket sock;
	ServerConfig& cfg;
	SessionWorld& world;
	std::unordered_map<std::string, PlayerRec>& players;
	std::unordered_set<std::string>& bannedKeys;
	std::unordered_map<std::string, Client> clients;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> appearAsm;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> invAsm;
	std::unordered_set<std::string> dirtyPlayers;
	bool worldDirty = false;
	std::uint32_t nextPeer = 1;
	std::uint32_t sessionModHash = 0;
	bool fakeWanted = false;
	bool fakeWasOn = false;
	int fakeCount = 1;
	std::unordered_map<std::string, cmp::RateBucket> queryRates;
	std::unordered_map<std::string, cmp::RateBucket> helloRates;
	std::unordered_map<std::string, cmp::RateBucket> hitRates;
	std::unordered_map<std::string, cmp::RateBucket> poseRates;
	std::unordered_map<std::string, cmp::RateBucket> chatRates;
	std::unordered_map<std::string, cmp::RateBucket> blobRates;
	std::uint64_t datagrams = 0;
	std::uint64_t badHeaders = 0;

	ServerRuntime(CmpSocket socket, ServerConfig& config, SessionWorld& sessionWorld,
		std::unordered_map<std::string, PlayerRec>& playerRecords,
		std::unordered_set<std::string>& bans, std::uint32_t modHash);

	void handle_packet(const char* buf, int n, const sockaddr_in& from, double t);
	void expire_clients(double t);
	void send_heartbeat();
	void send_unreliable(const Client& client, const void* data, int len, const char* what);
	void send_reliable(Client& client, const void* data, int len, const char* what);
	void send_blob_reliable(Client& client, cmp::Msg type, std::uint32_t peerId, const std::vector<std::uint8_t>& blob);
	void broadcast_session_rules();
	void tick_reliable(double t);
};

struct AdminContext {
	ServerRuntime& runtime;
	std::string_view configPath;
};

void run_admin_command(const std::string& cmdLine, AdminContext& ctx);

void tick_fake_peers(CmpSocket sock, std::unordered_map<std::string, Client>& clients,
	SessionWorld& world, int fakeCount, double t, std::uint64_t datagrams,
	std::uint64_t badHeaders, bool& fakeWasOn, FakeTickState& state);
