#include "handlers.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include "log.hpp"

ServerRuntime::ServerRuntime(ServerConfig& config, SessionWorld& sessionWorld,
	std::unordered_map<std::string, PlayerRec>& playerRecords, std::unordered_set<std::string>& bans,
	std::uint32_t modHash)
	: cfg(config), world(sessionWorld), players(playerRecords), bannedKeys(bans),
	  sessionModHash(modHash)
{
}

void ServerRuntime::send_tcp_conn(std::uint64_t connId, const void* data, int len, const char* what)
{
	if (!outbound || connId == 0 || !data || len <= 0) {
		return;
	}
	OutboundCmd cmd;
	cmd.kind = OutboundKind::TcpSend;
	cmd.connId = connId;
	cmd.bytes.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + len);
	if (!outbound->push(std::move(cmd))) {
		LOG_WARN("outbound full, drop tcp %s conn=%llu", what, static_cast<unsigned long long>(connId));
	}
}

void ServerRuntime::send_tcp(Client& client, const void* data, int len, const char* what)
{
	if (client.connId == 0) {
		return;
	}
	if (!outbound || !data || len <= 0) {
		return;
	}
	OutboundCmd cmd;
	cmd.kind = OutboundKind::TcpSend;
	cmd.connId = client.connId;
	cmd.peerId = client.peerId;
	cmd.bytes.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + len);
	if (!outbound->push(std::move(cmd))) {
		LOG_WARN("outbound full, drop tcp %s peer=%u", what, client.peerId);
	}
}

void ServerRuntime::send_udp_addr(const sockaddr_in& addr, const void* data, int len, const char* what)
{
	if (!outbound || !data || len <= 0) {
		return;
	}
	OutboundCmd cmd;
	cmd.kind = OutboundKind::UdpSend;
	cmd.addr = addr;
	cmd.bytes.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + len);
	if (!outbound->push(std::move(cmd))) {
		LOG_WARN("outbound full, drop udp %s", what);
	}
}

void ServerRuntime::send_udp(const Client& client, const void* data, int len, const char* what)
{
	if (!client.udpBound) {
		return;
	}
	send_udp_addr(client.udpAddr, data, len, what);
}

void ServerRuntime::send_blob_tcp(Client& client, cmp::Msg type, std::uint32_t peerId, const std::vector<std::uint8_t>& blob)
{
	std::vector<std::vector<std::uint8_t>> packets;
	if (!cmp::split_blob_chunks(type, peerId, blob, packets)) {
		return;
	}
	const auto what = std::string(cmp::msg_name(type));
	for (const auto& packet : packets) {
		send_tcp(client, packet.data(), static_cast<int>(packet.size()), what.c_str());
	}
}

void ServerRuntime::broadcast_session_rules()
{
	const auto rules = cmp::make_session_rules(build_session_flags(cfg));
	for (auto& [_, client] : clients) {
		send_tcp(client, &rules, sizeof(rules), "SessionRules");
	}
}

void ServerRuntime::reject_conn(std::uint64_t connId, cmp::RejectReason reason, const char* text)
{
	auto msg = cmp::make_reject(reason, text);
	send_tcp_conn(connId, &msg, sizeof(msg), "Reject");
	LOG_INFO("tx Reject %s (%s)", cmp::reject_name(reason), text);
}

void ServerRuntime::close_conn(std::uint64_t connId)
{
	if (!outbound || connId == 0) {
		return;
	}
	OutboundCmd cmd;
	cmd.kind = OutboundKind::CloseConn;
	cmd.connId = connId;
	outbound->push(std::move(cmd));
}

void ServerRuntime::bind_peer(std::uint64_t connId, std::uint32_t peerId)
{
	if (!outbound || connId == 0 || peerId == 0) {
		return;
	}
	OutboundCmd cmd;
	cmd.kind = OutboundKind::BindPeer;
	cmd.connId = connId;
	cmd.peerId = peerId;
	outbound->push(std::move(cmd));
}

void ServerRuntime::flush_dirty_async()
{
	if (!persist) {
		return;
	}
	PersistJob job;
	if (worldDirty) {
		job.world = true;
		job.worldSnap = world;
		worldDirty = false;
	}
	for (const auto& key : dirtyPlayers) {
		if (auto it = players.find(key); it != players.end()) {
			job.players.push_back(it->second);
		}
	}
	dirtyPlayers.clear();
	if (!job.world && job.players.empty()) {
		return;
	}
	if (!persist->enqueue(std::move(job))) {
		LOG_WARN("persist queue full");
	}
}

void ServerRuntime::persist_bans_async()
{
	if (!persist) {
		return;
	}
	PersistJob job;
	job.writeBans = true;
	job.banKeys.assign(bannedKeys.begin(), bannedKeys.end());
	if (!persist->enqueue(std::move(job))) {
		LOG_WARN("persist queue full (bans)");
	}
}

void ServerRuntime::unbind_udp(Client& client)
{
	if (!client.udpBound) {
		return;
	}
	udpPeerByAddr.erase(addr_key(client.udpAddr));
	client.udpBound = false;
	client.udpAddr = {};
}

Client* ServerRuntime::find_by_udp(const sockaddr_in& from)
{
	const auto key = addr_key(from);
	const auto it = udpPeerByAddr.find(key);
	if (it == udpPeerByAddr.end()) {
		return nullptr;
	}
	auto cit = clients.find(it->second);
	if (cit == clients.end()) {
		udpPeerByAddr.erase(it);
		return nullptr;
	}
	return &cit->second;
}

PendingTcp* ServerRuntime::find_pending(std::uint64_t connId)
{
	for (auto& pending : pendingTcp) {
		if (pending.connId == connId) {
			return &pending;
		}
	}
	return nullptr;
}

Client* ServerRuntime::find_by_conn(std::uint64_t connId)
{
	for (auto& [_, client] : clients) {
		if (client.connId == connId) {
			return &client;
		}
	}
	return nullptr;
}

void ServerRuntime::remove_client(std::uint32_t peerId, const char* reason, bool keepTcp)
{
	auto it = clients.find(peerId);
	if (it == clients.end()) {
		return;
	}
	LOG_INFO("remove peer=%u reason=%s", peerId, reason);
	log_json_event(cfg.jsonLog, std::string("{\"event\":\"leave\",\"peer\":") + std::to_string(peerId) + ",\"key\":\"" + it->second.playerKey + "\"}");
	if (auto pit = players.find(it->second.playerKey); pit != players.end()) {
		dirtyPlayers.insert(pit->first);
	}
	if (it->second.peerId == world.hostPeerId) {
		world.hostPeerId = 0;
	}
	const auto bye = cmp::make_bye(it->second.peerId);
	for (auto& [oid, other] : clients) {
		if (oid != peerId) {
			send_tcp(other, &bye, sizeof(bye), "Bye");
		}
	}
	unbind_udp(it->second);
	const auto connId = it->second.connId;
	if (keepTcp && connId != 0) {
		if (outbound) {
			OutboundCmd cmd;
			cmd.kind = OutboundKind::KeepPending;
			cmd.peerId = peerId;
			outbound->push(std::move(cmd));
		}
		PendingTcp keep;
		keep.connId = connId;
		keep.addr = it->second.tcpAddr;
		keep.connectedAt = now_sec();
		pendingTcp.push_back(std::move(keep));
	} else if (connId != 0) {
		close_conn(connId);
	}
	it->second.connId = 0;
	clients.erase(it);
	find_host(clients, world);
	flush_dirty_async();
}

void ServerRuntime::apply_inbound(InboundEvent& ev, double t)
{
	switch (ev.kind) {
	case InboundKind::TcpAccept: {
		PendingTcp pending;
		pending.connId = ev.connId;
		pending.addr = ev.addr;
		pending.connectedAt = t;
		pendingTcp.push_back(std::move(pending));
		LOG_INFO("sim pending conn=%llu from %s pending=%zu", static_cast<unsigned long long>(ev.connId),
			addr_key(ev.addr).c_str(), pendingTcp.size());
		break;
	}
	case InboundKind::TcpFrame: {
		if (ev.peerId != 0) {
			auto it = clients.find(ev.peerId);
			if (it != clients.end()) {
				removePeers.clear();
				keepTcpPeers.clear();
				handle_tcp_message(it->second, reinterpret_cast<const char*>(ev.bytes.data()),
					static_cast<int>(ev.bytes.size()), t);
				for (const auto peerId : removePeers) {
					const bool keep = std::find(keepTcpPeers.begin(), keepTcpPeers.end(), peerId) != keepTcpPeers.end();
					remove_client(peerId, "bye", keep);
				}
				removePeers.clear();
				keepTcpPeers.clear();
				break;
			}
		}
		if (auto* pending = find_pending(ev.connId)) {
			handle_pending_message(*pending, reinterpret_cast<const char*>(ev.bytes.data()),
				static_cast<int>(ev.bytes.size()), t);
			if (pending->connId == 0) {
				pendingTcp.erase(std::remove_if(pendingTcp.begin(), pendingTcp.end(),
					[](const PendingTcp& p) { return p.connId == 0; }),
					pendingTcp.end());
			}
		} else if (auto* client = find_by_conn(ev.connId)) {
			removePeers.clear();
			keepTcpPeers.clear();
			handle_tcp_message(*client, reinterpret_cast<const char*>(ev.bytes.data()),
				static_cast<int>(ev.bytes.size()), t);
			for (const auto peerId : removePeers) {
				const bool keep = std::find(keepTcpPeers.begin(), keepTcpPeers.end(), peerId) != keepTcpPeers.end();
				remove_client(peerId, "bye", keep);
			}
			removePeers.clear();
			keepTcpPeers.clear();
		}
		break;
	}
	case InboundKind::TcpClosed: {
		pendingTcp.erase(std::remove_if(pendingTcp.begin(), pendingTcp.end(),
			[&](const PendingTcp& p) { return p.connId == ev.connId; }),
			pendingTcp.end());
		if (ev.peerId != 0) {
			remove_client(ev.peerId, "tcp_closed", false);
		} else if (auto* client = find_by_conn(ev.connId)) {
			const auto peerId = client->peerId;
			client->connId = 0;
			remove_client(peerId, "tcp_closed", false);
		}
		break;
	}
	case InboundKind::UdpDatagram:
		handle_udp_packet(reinterpret_cast<const char*>(ev.bytes.data()), static_cast<int>(ev.bytes.size()),
			ev.addr, t);
		break;
	}
}

void ServerRuntime::handle_pending_message(PendingTcp& pending, const char* buf, int n, double t)
{
	const auto key = addr_key(pending.addr);
	if (n < static_cast<int>(sizeof(cmp::Header))) {
		++badHeaders;
		return;
	}
	cmp::Header header{};
	std::memcpy(&header, buf, sizeof(header));
	if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
		++badHeaders;
		return;
	}
	const auto type = static_cast<cmp::Msg>(header.type);
	if (type == cmp::Msg::SessionQuery && n >= static_cast<int>(sizeof(cmp::SessionQuery))) {
		if (!cmp::allow_rate(queryRates, key, t, 4)) {
			return;
		}
		auto* host = find_host(clients, world);
		const bool haveHost = host && host->havePose && world.hostPeerId != 0;
		const bool hostInterior = haveHost && world.hostLocation != 0 && world.hostLocation != cmp::kCommonwealthWorldspace;
		const auto info = cmp::make_session_info(
			haveHost ? world.hostPeerId : 0, haveHost ? world.hostLocation : 0,
			haveHost ? world.hostX : 0.0f, haveHost ? world.hostY : 0.0f, haveHost ? world.hostZ : 0.0f,
			static_cast<std::uint32_t>(clients.size()), haveHost, hostInterior, cfg.name,
			static_cast<std::uint32_t>(cfg.maxPlayers), cfg.motd, build_session_flags(cfg));
		send_tcp_conn(pending.connId, &info, sizeof(info), "SessionInfo");
		LOG_INFO("SessionQuery from %s haveHost=%d clients=%zu", key.c_str(), haveHost ? 1 : 0, clients.size());
		return;
	}
	if (type != cmp::Msg::Hello || n < static_cast<int>(sizeof(cmp::Hello))) {
		LOG_DEBUG("pending tcp unexpected type=%u from %s", static_cast<unsigned>(header.type), key.c_str());
		return;
	}
	if (!cmp::allow_rate(helloRates, key, t, 2)) {
		return;
	}
	cmp::Hello hello{};
	std::memcpy(&hello, buf, sizeof(hello));
	hello.name[31] = '\0';
	hello.playerKey[31] = '\0';
	hello.password[15] = '\0';
	if (hello.protocol != cmp::kProtocolVersion || header.version != cmp::kProtocolVersion) {
		reject_conn(pending.connId, cmp::RejectReason::Protocol, "need protocol 12");
		return;
	}
	if (hello.pluginVersion != cmp::kPluginVersion) {
		reject_conn(pending.connId, cmp::RejectReason::PluginVersion, "plugin version mismatch");
		return;
	}
	if (!hello.inWorld) {
		reject_conn(pending.connId, cmp::RejectReason::NotInWorld, "load a save and enter the world");
		return;
	}
	const auto pkey = cmp::sanitize_player_key(hello.playerKey, "player");
	if (bannedKeys.contains(pkey)) {
		reject_conn(pending.connId, cmp::RejectReason::Banned, "banned");
		return;
	}
	if (!cfg.password.empty()) {
		if (hello.password[0] == '\0') {
			reject_conn(pending.connId, cmp::RejectReason::Password, "password required");
			return;
		}
		if (cfg.password != hello.password) {
			reject_conn(pending.connId, cmp::RejectReason::Password, "wrong password");
			return;
		}
	}
	if (sessionModHash != 0 && hello.modHash != sessionModHash) {
		reject_conn(pending.connId, cmp::RejectReason::ModMismatch, "mod list mismatch");
		return;
	}
	const bool requireHost = (hello.flags & cmp::kHelloFlagRequireHost) != 0;
	if (requireHost) {
		auto* liveHost = find_host(clients, world);
		if (!liveHost || !liveHost->havePose || world.hostPeerId == 0) {
			reject_conn(pending.connId, cmp::RejectReason::NoHost, "need a live host in the world");
			return;
		}
		if (world.hostLocation != cmp::kCommonwealthWorldspace) {
			reject_conn(pending.connId, cmp::RejectReason::HostNotStreaming, "host is not Commonwealth exterior");
			return;
		}
	}

	auto reconnect = clients.end();
	for (auto candidate = clients.begin(); candidate != clients.end(); ++candidate) {
		if (candidate->second.playerKey == pkey) {
			reconnect = candidate;
			break;
		}
	}
	const bool softReconnect = reconnect != clients.end();
	if (!softReconnect && static_cast<int>(clients.size()) >= cfg.maxPlayers) {
		reject_conn(pending.connId, cmp::RejectReason::Full, "server full");
		log_json_event(cfg.jsonLog, "{\"event\":\"full\",\"addr\":\"" + key + "\"}");
		return;
	}

	auto& rec = players[pkey];
	rec.key = pkey;
	if (rec.name.empty()) {
		rec.name = hello.name[0] ? hello.name : "player";
	}
	const bool isNew = !rec.havePose;
	if (!world.created) {
		world.created = true;
		worldDirty = true;
		LOG_INFO("world created spawn Sanctuary");
	}

	Client* joined = nullptr;
	const auto pendingConn = pending.connId;
	if (softReconnect) {
		unbind_udp(reconnect->second);
		if (reconnect->second.connId != 0 && reconnect->second.connId != pendingConn) {
			close_conn(reconnect->second.connId);
		}
		reconnect->second.connId = pendingConn;
		reconnect->second.tcpAddr = pending.addr;
		reconnect->second.lastSeen = t;
		reconnect->second.playerKey = pkey;
		if (hello.name[0]) {
			reconnect->second.name = hello.name;
		}
		reconnect->second.udpToken = cmp::make_udp_token();
		reconnect->second.udpBound = false;
		if (rec.appearance.empty() == false) {
			reconnect->second.appearance = rec.appearance;
		}
		if (rec.inventory.empty() == false) {
			reconnect->second.inventory = rec.inventory;
		}
		if (!reconnect->second.havePose && rec.havePose) {
			reconnect->second.lastPose = cmp::make_pose(reconnect->second.peerId, rec.locationFormId, rec.x, rec.y, rec.z, rec.yaw);
			reconnect->second.havePose = true;
		}
		joined = &reconnect->second;
		bind_peer(pendingConn, joined->peerId);
		LOG_INFO("Hello SOFT_RECONNECT %s key=%s peer=%u", key.c_str(), pkey.c_str(), joined->peerId);
	} else {
		Client client;
		client.connId = pendingConn;
		client.tcpAddr = pending.addr;
		client.peerId = alloc_peer(nextPeer);
		client.udpToken = cmp::make_udp_token();
		client.playerKey = pkey;
		client.name = hello.name[0] ? hello.name : rec.name;
		client.lastSeen = t;
		client.lastPose = isNew
			? cmp::make_pose(client.peerId, world.spawnLocation, world.spawnX, world.spawnY, world.spawnZ, 0.0f)
			: cmp::make_pose(client.peerId, rec.locationFormId, rec.x, rec.y, rec.z, rec.yaw);
		client.havePose = true;
		client.appearance = rec.appearance;
		client.inventory = rec.inventory;
		const auto peerId = client.peerId;
		joined = &clients.emplace(peerId, std::move(client)).first->second;
		if (!world.hostPeerId) {
			if (requireHost) {
				reject_conn(joined->connId, cmp::RejectReason::NoHost, "need a live host in the world");
				close_conn(joined->connId);
				clients.erase(peerId);
				pending.connId = 0;
				return;
			}
			world.hostPeerId = peerId;
		}
		bind_peer(pendingConn, peerId);
		LOG_INFO("Hello NEW %s key=%s name=%s peer=%u new=%d host=%d clients=%zu",
			key.c_str(), pkey.c_str(), joined->name.c_str(), joined->peerId, isNew ? 1 : 0,
			joined->peerId == world.hostPeerId ? 1 : 0, clients.size());
		log_json_event(cfg.jsonLog, std::string("{\"event\":\"join\",\"peer\":") + std::to_string(joined->peerId)
			+ ",\"key\":\"" + pkey + "\",\"name\":\"" + joined->name + "\"}");
	}

	pending.connId = 0; // ownership moved to Client

	if (joined->peerId == world.hostPeerId) {
		world.gameHour = hello.gameHour;
		world.gameDaysPassed = hello.gameDaysPassed;
		world.weatherFormId = hello.weatherFormId;
		world.hostLocation = hello.locationFormId ? hello.locationFormId : world.hostLocation;
		world.hostX = hello.x;
		world.hostY = hello.y;
		world.hostZ = hello.z;
		worldDirty = true;
		if (sessionModHash == 0 && hello.modHash != 0) {
			sessionModHash = hello.modHash;
			LOG_INFO("mod_hash locked from host 0x%08X", sessionModHash);
		}
	}

	const bool isHost = joined->peerId == world.hostPeerId;
	const auto welcome = cmp::make_welcome(joined->peerId, 0, isNew, isHost,
		build_session_flags(cfg), joined->udpToken);
	send_tcp(*joined, &welcome, sizeof(welcome), "Welcome");
	LOG_INFO("tx Welcome peer=%u token=%u new=%d host=%d -> %s",
		welcome.peerId, welcome.udpToken, isNew ? 1 : 0, isHost ? 1 : 0, key.c_str());

	std::uint32_t placeLoc = world.hostLocation;
	float px = world.hostX, py = world.hostY, pz = world.hostZ;
	if (requireHost) {
		px += cmp::kGuestSpawnOffsetX;
	} else if (!isNew && rec.havePose && rec.locationFormId != 0 && rec.locationFormId == world.hostLocation) {
		placeLoc = rec.locationFormId;
		px = rec.x;
		py = rec.y;
		pz = rec.z;
	}
	const auto snap = cmp::make_world_snapshot(world.gameHour, world.gameDaysPassed, world.weatherFormId,
		world.hostLocation, world.hostX, world.hostY, world.hostZ, world.hostPeerId, placeLoc, px, py, pz, isNew);
	send_tcp(*joined, &snap, sizeof(snap), "WorldSnapshot");
	for (auto& [oid, other] : clients) {
		if (oid == joined->peerId || !other.havePose) {
			continue;
		}
		send_udp(*joined, &other.lastPose, sizeof(other.lastPose), "PlayerPose");
		if (!other.appearance.empty()) {
			send_blob_tcp(*joined, cmp::Msg::AppearanceChunk, other.peerId, other.appearance);
		}
		if (!other.inventory.empty()) {
			send_blob_tcp(*joined, cmp::Msg::InventoryChunk, other.peerId, other.inventory);
		}
	}
}

void ServerRuntime::handle_tcp_message(Client& client, const char* buf, int n, double t)
{
	if (n < static_cast<int>(sizeof(cmp::Header))) {
		++badHeaders;
		return;
	}
	cmp::Header header{};
	std::memcpy(&header, buf, sizeof(header));
	if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
		++badHeaders;
		return;
	}
	const auto type = static_cast<cmp::Msg>(header.type);
	if (!cmp::msg_is_tcp(type)) {
		LOG_DEBUG("tcp got udp-only type=%u peer=%u", static_cast<unsigned>(header.type), client.peerId);
		return;
	}
	client.lastSeen = t;
	const auto rateKey = std::to_string(client.peerId);

	if (type == cmp::Msg::Bye) {
		removePeers.push_back(client.peerId);
		keepTcpPeers.push_back(client.peerId);
		return;
	}
	if (type == cmp::Msg::Hello && n >= static_cast<int>(sizeof(cmp::Hello))) {
		client.udpToken = cmp::make_udp_token();
		unbind_udp(client);
		const bool isNew = !players[client.playerKey].havePose;
		const bool isHost = client.peerId == world.hostPeerId;
		const auto welcome = cmp::make_welcome(client.peerId, 0, isNew, isHost,
			build_session_flags(cfg), client.udpToken);
		send_tcp(client, &welcome, sizeof(welcome), "Welcome");
		LOG_INFO("Hello again peer=%u token=%u", client.peerId, client.udpToken);
		return;
	}
	if (type == cmp::Msg::WorldSnapshot && n >= static_cast<int>(sizeof(cmp::WorldSnapshot))) {
		cmp::WorldSnapshot snap{};
		std::memcpy(&snap, buf, sizeof(snap));
		if (client.peerId == world.hostPeerId) {
			world.gameHour = snap.gameHour;
			world.gameDaysPassed = snap.gameDaysPassed;
			world.weatherFormId = snap.weatherFormId;
			world.hostLocation = snap.hostLocationFormId ? snap.hostLocationFormId : world.hostLocation;
			world.hostX = snap.hostX;
			world.hostY = snap.hostY;
			world.hostZ = snap.hostZ;
			worldDirty = true;
			LOG_INFO("WorldSnapshot from host peer %u hour=%.2f days=%.2f", client.peerId, snap.gameHour, snap.gameDaysPassed);
		}
		return;
	}
	if ((type == cmp::Msg::AppearanceChunk || type == cmp::Msg::InventoryChunk) && n >= static_cast<int>(sizeof(cmp::BlobChunk))) {
		if (!cmp::allow_rate(blobRates, rateKey, t, 64)) {
			return;
		}
		std::vector<std::uint8_t> blob;
		auto& assemblies = type == cmp::Msg::AppearanceChunk ? appearAsm : invAsm;
		if (!take_blob(assemblies[client.peerId], buf, n, blob)) {
			return;
		}
		assemblies.erase(client.peerId);
		auto& rec = players[client.playerKey];
		rec.key = client.playerKey;
		if (type == cmp::Msg::AppearanceChunk) {
			client.appearance = rec.appearance = blob;
		} else {
			client.inventory = rec.inventory = blob;
		}
		dirtyPlayers.insert(rec.key);
		for (auto& [oid, other] : clients) {
			if (oid != client.peerId) {
				send_blob_tcp(other, type, client.peerId, blob);
			}
		}
		LOG_INFO("%s peer=%u key=%s bytes=%zu", type == cmp::Msg::AppearanceChunk ? "Appearance" : "Inventory",
			client.peerId, client.playerKey.c_str(), blob.size());
		return;
	}
	if (type == cmp::Msg::Heartbeat && n >= static_cast<int>(sizeof(cmp::Heartbeat))) {
		cmp::Heartbeat hb{};
		std::memcpy(&hb, buf, sizeof(hb));
		hb.peerId = client.peerId;
		cmp::fill_header(hb, cmp::Msg::Heartbeat);
		send_tcp(client, &hb, sizeof(hb), "Heartbeat");
		return;
	}
	if (type == cmp::Msg::Chat && n >= static_cast<int>(sizeof(cmp::Chat))) {
		cmp::Chat chat{};
		std::memcpy(&chat, buf, sizeof(chat));
		chat.text[sizeof(chat.text) - 1] = '\0';
		if (!cmp::allow_rate(chatRates, rateKey, t, 4)) {
			return;
		}
		chat.fromPeerId = client.peerId;
		cmp::fill_header(chat, cmp::Msg::Chat);
		for (auto& [oid, other] : clients) {
			if (oid != client.peerId) {
				send_tcp(other, &chat, sizeof(chat), "Chat");
			}
		}
		LOG_INFO("Chat from peer=%u name=%s: %s", client.peerId, client.name.c_str(), chat.text);
		return;
	}
	if (type == cmp::Msg::Kick && n >= static_cast<int>(sizeof(cmp::Kick))) {
		cmp::Kick kick{};
		std::memcpy(&kick, buf, sizeof(kick));
		kick.reason[sizeof(kick.reason) - 1] = '\0';
		if (client.peerId == world.hostPeerId) {
			for (auto& [_, other] : clients) {
				if (other.peerId == kick.targetPeerId) {
					send_tcp(other, &kick, sizeof(kick), "Kick");
					LOG_INFO("Host kicked peer=%u reason=%s", kick.targetPeerId, kick.reason);
					break;
				}
			}
		}
		return;
	}
	if (type == cmp::Msg::Teleport && n >= static_cast<int>(sizeof(cmp::Teleport))) {
		cmp::Teleport tp{};
		std::memcpy(&tp, buf, sizeof(tp));
		if (client.peerId == world.hostPeerId) {
			for (auto& [_, other] : clients) {
				if (other.peerId == tp.targetPeerId) {
					send_tcp(other, &tp, sizeof(tp), "Teleport");
					LOG_INFO("Host teleport peer=%u to (%.0f,%.0f,%.0f) loc=%X", tp.targetPeerId, tp.x, tp.y, tp.z, tp.locationFormId);
					break;
				}
			}
		}
		return;
	}
	if (type == cmp::Msg::Hit && n >= static_cast<int>(sizeof(cmp::Hit))) {
		if (!cfg.pvp) {
			return;
		}
		cmp::Hit hit{};
		std::memcpy(&hit, buf, sizeof(hit));
		if (!cmp::allow_rate(hitRates, rateKey, t, 24)) {
			return;
		}
		if (hit.attackerPeerId != client.peerId || hit.targetPeerId == 0
			|| hit.targetPeerId == hit.attackerPeerId) {
			return;
		}
		hit.damage = cmp::clamp_hit_damage(hit.damage);
		if (hit.damage <= 0.0f) {
			return;
		}
		cmp::fill_header(hit, cmp::Msg::Hit);
		for (auto& [_, other] : clients) {
			if (other.peerId == hit.targetPeerId) {
				send_tcp(other, &hit, sizeof(hit), "Hit");
			}
		}
		return;
	}
	LOG_DEBUG("unhandled tcp msg type=%u peer=%u", static_cast<unsigned>(header.type), client.peerId);
}

void ServerRuntime::handle_udp_packet(const char* buf, int n, const sockaddr_in& from, double t)
{
	++datagrams;
	if (n < static_cast<int>(sizeof(cmp::Header))) {
		++badHeaders;
		return;
	}
	cmp::Header header{};
	std::memcpy(&header, buf, sizeof(header));
	if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
		++badHeaders;
		return;
	}
	const auto type = static_cast<cmp::Msg>(header.type);
	if (!cmp::msg_is_udp(type)) {
		LOG_DEBUG("udp got tcp-only type=%u from %s", static_cast<unsigned>(header.type), addr_key(from).c_str());
		return;
	}

	if (type == cmp::Msg::UdpBind && n >= static_cast<int>(sizeof(cmp::UdpBind))) {
		cmp::UdpBind bindMsg{};
		std::memcpy(&bindMsg, buf, sizeof(bindMsg));
		auto it = clients.find(bindMsg.peerId);
		if (it == clients.end() || it->second.udpToken == 0 || it->second.udpToken != bindMsg.udpToken) {
			LOG_DEBUG("UdpBind rejected peer=%u from %s", bindMsg.peerId, addr_key(from).c_str());
			return;
		}
		unbind_udp(it->second);
		it->second.udpAddr = from;
		it->second.udpBound = true;
		it->second.lastSeen = t;
		udpPeerByAddr[addr_key(from)] = it->second.peerId;
		LOG_INFO("UdpBind peer=%u addr=%s", it->second.peerId, addr_key(from).c_str());
		for (auto& [oid, other] : clients) {
			if (oid == it->second.peerId || !other.havePose) {
				continue;
			}
			send_udp(it->second, &other.lastPose, sizeof(other.lastPose), "PlayerPose");
		}
		return;
	}

	Client* client = find_by_udp(from);
	if (!client) {
		LOG_DEBUG("udp from unbound %s type=%u", addr_key(from).c_str(), static_cast<unsigned>(header.type));
		return;
	}
	client->lastSeen = t;
	const auto rateKey = std::to_string(client->peerId);

	if (type == cmp::Msg::PlayerPose && n >= static_cast<int>(sizeof(cmp::PlayerPose))) {
		if (!cmp::allow_rate(poseRates, rateKey, t, 30)) {
			return;
		}
		cmp::PlayerPose pose{};
		std::memcpy(&pose, buf, sizeof(pose));
		++client->posesIn;
		pose.peerId = client->peerId;
		cmp::fill_header(pose, cmp::Msg::PlayerPose);
		client->lastPose = pose;
		client->havePose = true;
		auto& rec = players[client->playerKey];
		rec.key = client->playerKey;
		rec.havePose = true;
		rec.locationFormId = pose.locationFormId;
		rec.x = pose.x;
		rec.y = pose.y;
		rec.z = pose.z;
		rec.yaw = pose.yaw;
		dirtyPlayers.insert(rec.key);
		if (client->peerId == world.hostPeerId) {
			world.hostLocation = pose.locationFormId ? pose.locationFormId : world.hostLocation;
			world.hostX = pose.x;
			world.hostY = pose.y;
			world.hostZ = pose.z;
			worldDirty = true;
		}
		for (auto& [oid, other] : clients) {
			if (oid != client->peerId && other.udpBound && in_interest(*client, other, cfg.interestUu)) {
				send_udp(other, &pose, sizeof(pose), "PlayerPose");
			}
		}
		return;
	}
	if (type == cmp::Msg::ActorPose && n >= static_cast<int>(sizeof(cmp::ActorPose))) {
		if (client->peerId != world.hostPeerId) {
			return;
		}
		cmp::ActorPose pose{};
		std::memcpy(&pose, buf, sizeof(pose));
		cmp::fill_header(pose, cmp::Msg::ActorPose);
		for (auto& [oid, other] : clients) {
			if (oid != client->peerId && other.udpBound
				&& cmp::in_interest(pose, other.lastPose, other.havePose, cfg.interestUu)) {
				send_udp(other, &pose, sizeof(pose), "ActorPose");
			}
		}
		return;
	}
}

void ServerRuntime::expire_pending(double t)
{
	for (auto it = pendingTcp.begin(); it != pendingTcp.end();) {
		if (t - it->connectedAt > kPendingTcpTimeoutSec) {
			LOG_INFO("pending tcp timeout %s", addr_key(it->addr).c_str());
			close_conn(it->connId);
			it = pendingTcp.erase(it);
			continue;
		}
		++it;
	}
}

void ServerRuntime::expire_clients(double t)
{
	std::vector<std::uint32_t> dead;
	for (const auto& [peerId, client] : clients) {
		if (t - client.lastSeen > kClientTimeoutSec) {
			dead.push_back(peerId);
		}
	}
	for (const auto peerId : dead) {
		remove_client(peerId, "timeout");
	}
}

void ServerRuntime::send_heartbeat()
{
	const auto hb = cmp::make_heartbeat(0, 0);
	for (auto& [_, client] : clients) {
		send_tcp(client, &hb, sizeof(hb), "Heartbeat");
	}
}
