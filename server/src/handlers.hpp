#pragma once

#include <string_view>
#include <vector>

#include "persist_worker.hpp"
#include "queues.hpp"
#include "server_state.hpp"

struct ServerRuntime {
	ServerConfig& cfg;
	SessionWorld& world;
	std::unordered_map<std::string, PlayerRec>& players;
	std::unordered_set<std::string>& bannedKeys;
	std::unordered_map<std::uint32_t, Client> clients;
	std::unordered_map<std::string, std::uint32_t> udpPeerByAddr;
	std::vector<PendingTcp> pendingTcp;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> appearAsm;
	std::unordered_map<std::uint32_t, cmp::BlobAssembly> invAsm;
	std::unordered_set<std::string> dirtyPlayers;
	bool worldDirty = false;
	std::uint32_t nextPeer = 1;
	std::uint32_t sessionModHash = 0;
	std::unordered_map<std::string, cmp::RateBucket> queryRates;
	std::unordered_map<std::string, cmp::RateBucket> helloRates;
	std::unordered_map<std::string, cmp::RateBucket> hitRates;
	std::unordered_map<std::string, cmp::RateBucket> poseRates;
	std::unordered_map<std::string, cmp::RateBucket> chatRates;
	std::unordered_map<std::string, cmp::RateBucket> blobRates;
	std::uint64_t datagrams = 0;
	std::uint64_t badHeaders = 0;

	AsyncQueue<OutboundCmd>* outbound = nullptr;
	PersistWorker* persist = nullptr;

	ServerRuntime(ServerConfig& config, SessionWorld& sessionWorld,
		std::unordered_map<std::string, PlayerRec>& playerRecords,
		std::unordered_set<std::string>& bans, std::uint32_t modHash);

	void apply_inbound(InboundEvent& ev, double t);
	void handle_udp_packet(const char* buf, int n, const sockaddr_in& from, double t);
	void handle_tcp_message(Client& client, const char* buf, int n, double t);
	void handle_pending_message(PendingTcp& pending, const char* buf, int n, double t);
	void remove_client(std::uint32_t peerId, const char* reason, bool keepTcp = false);
	void expire_clients(double t);
	void expire_pending(double t);
	void send_heartbeat();
	void send_udp(const Client& client, const void* data, int len, const char* what);
	void send_udp_addr(const sockaddr_in& addr, const void* data, int len, const char* what);
	void send_tcp(Client& client, const void* data, int len, const char* what);
	void send_tcp_conn(std::uint64_t connId, const void* data, int len, const char* what);
	void send_blob_tcp(Client& client, cmp::Msg type, std::uint32_t peerId, const std::vector<std::uint8_t>& blob);
	void broadcast_session_rules();
	void reject_conn(std::uint64_t connId, cmp::RejectReason reason, const char* text);
	void close_conn(std::uint64_t connId);
	void bind_peer(std::uint64_t connId, std::uint32_t peerId);
	void flush_dirty_async();
	void persist_bans_async();
	Client* find_by_udp(const sockaddr_in& from);
	void unbind_udp(Client& client);
	PendingTcp* find_pending(std::uint64_t connId);
	Client* find_by_conn(std::uint64_t connId);

	std::vector<std::uint32_t> removePeers;
	std::vector<std::uint32_t> keepTcpPeers;
};

struct AdminContext {
	ServerRuntime& runtime;
	std::string_view configPath;
};

void run_admin_command(const std::string& cmdLine, AdminContext& ctx);
