#include "handlers.hpp"

#include <cstring>
#include <vector>

#include "log.hpp"

ServerRuntime::ServerRuntime(CmpSocket socket, ServerConfig& config, SessionWorld& sessionWorld,
	std::unordered_map<std::string, PlayerRec>& playerRecords, std::unordered_set<std::string>& bans,
	std::uint32_t modHash)
	: sock(socket), cfg(config), world(sessionWorld), players(playerRecords), bannedKeys(bans), sessionModHash(modHash),
	  fakeWanted(config.fake), fakeCount(cmp::clamp_fake_count(config.fakeCount)) {}

void ServerRuntime::send_unreliable(const Client& client, const void* data, int len, const char* what)
{
	send_to(sock, client.addr, data, len, what);
}

void ServerRuntime::send_reliable(Client& client, const void* data, int len, const char* what)
{
	if (!data || len < static_cast<int>(sizeof(cmp::Header))) return;
	std::vector<std::uint8_t> packet(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + len);
	const auto seq = client.reliable.stamp(packet.data(), len);
	client.reliable.track(seq, packet.data(), len, now_sec());
	send_to(sock, client.addr, packet.data(), len, what);
}

void ServerRuntime::send_blob_reliable(Client& client, cmp::Msg type, std::uint32_t peerId, const std::vector<std::uint8_t>& blob)
{
	std::vector<std::vector<std::uint8_t>> packets;
	if (!cmp::split_blob_chunks(type, peerId, blob, packets)) return;
	const auto what = std::string(cmp::msg_name(type));
	for (const auto& packet : packets) send_reliable(client, packet.data(), static_cast<int>(packet.size()), what.c_str());
}

void ServerRuntime::broadcast_session_rules()
{
	const auto rules = cmp::make_session_rules(build_session_flags(cfg));
	for (auto& [_, client] : clients) send_reliable(client, &rules, sizeof(rules), "SessionRules");
}

void ServerRuntime::tick_reliable(double t)
{
	for (auto& [_, client] : clients) {
		client.reliable.tick(t, [this, &client](const void* data, int len) {
			send_to(sock, client.addr, data, len, "ReliableRetry");
		});
	}
}

void ServerRuntime::handle_packet(const char* buf, int n, const sockaddr_in& from, double t)
{
	++datagrams;
	const auto key = addr_key(from);
	if (n < static_cast<int>(sizeof(cmp::Header))) { ++badHeaders; return; }
	cmp::Header header{};
	std::memcpy(&header, buf, sizeof(header));
	if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
		++badHeaders;
		LOG_DEBUG("bad header from %s type=%u ver=%u size=%u nbytes=%d", key.c_str(), static_cast<unsigned>(header.type), static_cast<unsigned>(header.version), static_cast<unsigned>(header.size), n);
		return;
	}
	const auto type = static_cast<cmp::Msg>(header.type);
	auto sender = clients.find(key);
	if (type == cmp::Msg::Ack && n >= static_cast<int>(sizeof(cmp::Ack))) {
		cmp::Ack ack{};
		std::memcpy(&ack, buf, sizeof(ack));
		if (sender != clients.end()) sender->second.reliable.on_ack(ack.ackSeq, t);
		return;
	}
	if ((header.flags & cmp::HeaderFlag::Reliable) != 0 && header.seq != 0) {
		const auto ack = cmp::make_ack(header.seq, sender == clients.end() ? 0 : sender->second.peerId);
		send_to(sock, from, &ack, sizeof(ack), "Ack");
		if (sender != clients.end() && sender->second.reliable.already_handled(header.seq, t)) return;
	}
	if (type == cmp::Msg::SessionQuery && n >= static_cast<int>(sizeof(cmp::SessionQuery))) {
		if (!cmp::allow_rate(queryRates, key, t, 4)) { LOG_DEBUG("rate-limit SessionQuery from %s", key.c_str()); return; }
		auto* host = find_host(clients, world);
		const bool haveHost = host && host->havePose && world.hostPeerId != 0;
		const bool hostInterior = haveHost && world.hostLocation != 0 && world.hostLocation != cmp::kCommonwealthWorldspace;
		const auto info = cmp::make_session_info(haveHost ? world.hostPeerId : 0, haveHost ? world.hostLocation : 0, haveHost ? world.hostX : 0.0f, haveHost ? world.hostY : 0.0f, haveHost ? world.hostZ : 0.0f, static_cast<std::uint32_t>(clients.size()), haveHost, hostInterior, cfg.name, static_cast<std::uint32_t>(cfg.maxPlayers), cfg.motd, build_session_flags(cfg));
		send_to(sock, from, &info, sizeof(info), "SessionInfo");
		LOG_INFO("SessionQuery from %s haveHost=%d host=%u loc=%X clients=%zu/%d name=%s", key.c_str(), haveHost ? 1 : 0, info.hostPeerId, info.hostLocationFormId, clients.size(), cfg.maxPlayers, cfg.name.c_str());
		return;
	}
	if (type == cmp::Msg::Hello && n >= static_cast<int>(sizeof(cmp::Hello))) {
		if (!cmp::allow_rate(helloRates, key, t, 2)) { LOG_DEBUG("rate-limit Hello from %s", key.c_str()); return; }
		cmp::Hello hello{};
		std::memcpy(&hello, buf, sizeof(hello));
		hello.name[31] = '\0'; hello.playerKey[31] = '\0'; hello.password[15] = '\0';
		if (hello.protocol != cmp::kProtocolVersion || header.version != cmp::kProtocolVersion) { reject_to(sock, from, cmp::RejectReason::Protocol, "need protocol 11"); return; }
		if (hello.pluginVersion != cmp::kPluginVersion) { reject_to(sock, from, cmp::RejectReason::PluginVersion, "plugin version mismatch"); return; }
		if (!hello.inWorld) { reject_to(sock, from, cmp::RejectReason::NotInWorld, "load a save and enter the world"); return; }
		const auto pkey = cmp::sanitize_player_key(hello.playerKey, "player");
		if (bannedKeys.contains(pkey)) { reject_to(sock, from, cmp::RejectReason::Banned, "banned"); return; }
		if (!cfg.password.empty()) {
			if (hello.password[0] == '\0') { reject_to(sock, from, cmp::RejectReason::Password, "password required"); return; }
			if (cfg.password != hello.password) { reject_to(sock, from, cmp::RejectReason::Password, "wrong password"); return; }
		}
		if (sessionModHash != 0 && hello.modHash != sessionModHash) { reject_to(sock, from, cmp::RejectReason::ModMismatch, "mod list mismatch"); return; }
		const bool requireHost = (hello.flags & cmp::kHelloFlagRequireHost) != 0;
		if (requireHost) {
			auto* liveHost = find_host(clients, world);
			if (!liveHost || !liveHost->havePose || world.hostPeerId == 0) { reject_to(sock, from, cmp::RejectReason::NoHost, "need a live host in the world"); return; }
			if (world.hostLocation != cmp::kCommonwealthWorldspace) { reject_to(sock, from, cmp::RejectReason::HostNotStreaming, "host is not Commonwealth exterior"); return; }
		}
		auto existing = clients.find(key);
		auto reconnect = clients.end();
		if (existing == clients.end()) {
			for (auto candidate = clients.begin(); candidate != clients.end(); ++candidate) {
				if (candidate->second.playerKey == pkey) {
					reconnect = candidate;
					break;
				}
			}
		}
		const bool softReconnect = reconnect != clients.end();
		if (existing == clients.end() && reconnect == clients.end() && static_cast<int>(clients.size()) >= cfg.maxPlayers) {
			reject_to(sock, from, cmp::RejectReason::Full, "server full");
			log_json_event(cfg.jsonLog, "{\"event\":\"full\",\"addr\":\"" + key + "\"}");
			return;
		}
		auto& rec = players[pkey];
		rec.key = pkey;
		if (rec.name.empty()) rec.name = hello.name[0] ? hello.name : "player";
		const bool isNew = !rec.havePose;
		if (!world.created) { world.created = true; worldDirty = true; LOG_INFO("world created spawn Sanctuary"); }
		auto it = clients.find(key);
		if (it == clients.end()) {
			if (softReconnect) {
				Client client = std::move(reconnect->second);
				const auto oldKey = reconnect->first;
				clients.erase(reconnect);
				client.addr = from;
				client.lastSeen = t;
				client.playerKey = pkey;
				if (hello.name[0]) client.name = hello.name;
				client.appearance = rec.appearance.empty() ? client.appearance : rec.appearance;
				client.inventory = rec.inventory.empty() ? client.inventory : rec.inventory;
				if (!client.havePose && rec.havePose) {
					client.lastPose = cmp::make_pose(client.peerId, rec.locationFormId, rec.x, rec.y, rec.z, rec.yaw);
					client.havePose = true;
				}
				it = clients.emplace(key, std::move(client)).first;
				LOG_INFO("Hello SOFT_RECONNECT %s -> %s key=%s peer=%u", oldKey.c_str(), key.c_str(), pkey.c_str(), it->second.peerId);
			} else {
				Client client;
				client.addr = from; client.peerId = alloc_peer(nextPeer); client.playerKey = pkey;
				client.name = hello.name[0] ? hello.name : rec.name; client.lastSeen = t;
				client.lastPose = isNew ? cmp::make_pose(client.peerId, world.spawnLocation, world.spawnX, world.spawnY, world.spawnZ, 0.0f) : cmp::make_pose(client.peerId, rec.locationFormId, rec.x, rec.y, rec.z, rec.yaw);
				client.havePose = true; client.appearance = rec.appearance; client.inventory = rec.inventory;
				it = clients.emplace(key, std::move(client)).first;
			}
			if (!world.hostPeerId) {
				if (requireHost) { clients.erase(it); reject_to(sock, from, cmp::RejectReason::NoHost, "need a live host in the world"); return; }
				world.hostPeerId = it->second.peerId;
			}
			if (!softReconnect) {
				LOG_INFO("Hello NEW %s key=%s name=%s peer=%u new=%d host=%d clients=%zu", key.c_str(), pkey.c_str(), it->second.name.c_str(), it->second.peerId, isNew ? 1 : 0, it->second.peerId == world.hostPeerId ? 1 : 0, clients.size());
				log_json_event(cfg.jsonLog, std::string("{\"event\":\"join\",\"peer\":") + std::to_string(it->second.peerId) + ",\"key\":\"" + pkey + "\",\"name\":\"" + it->second.name + "\"}");
			}
		} else {
			it->second.lastSeen = t; it->second.addr = from; it->second.playerKey = pkey;
			LOG_INFO("Hello again %s key=%s peer %u", key.c_str(), pkey.c_str(), it->second.peerId);
		}
		if (it->second.peerId == world.hostPeerId) {
			world.gameHour = hello.gameHour; world.gameDaysPassed = hello.gameDaysPassed; world.weatherFormId = hello.weatherFormId; world.hostLocation = hello.locationFormId ? hello.locationFormId : world.hostLocation; world.hostX = hello.x; world.hostY = hello.y; world.hostZ = hello.z; worldDirty = true;
			if (sessionModHash == 0 && hello.modHash != 0) { sessionModHash = hello.modHash; LOG_INFO("mod_hash locked from host 0x%08X", sessionModHash); }
		}
		const bool fakeNow = fakeWanted && clients.size() < 2;
		const bool isHost = it->second.peerId == world.hostPeerId;
		const auto welcome = cmp::make_welcome(it->second.peerId, fakeNow ? cmp::kFakePeerId : 0, isNew, isHost, build_session_flags(cfg));
		send_reliable(it->second, &welcome, sizeof(welcome), "Welcome");
		LOG_INFO("tx Welcome peer=%u fake=%u new=%d host=%d -> %s", welcome.peerId, welcome.fakePeerId, isNew ? 1 : 0, isHost ? 1 : 0, key.c_str());
		std::uint32_t placeLoc = world.hostLocation; float px = world.hostX, py = world.hostY, pz = world.hostZ;
		if (requireHost) px += cmp::kGuestSpawnOffsetX;
		else if (!isNew && rec.havePose && rec.locationFormId != 0 && rec.locationFormId == world.hostLocation) { placeLoc = rec.locationFormId; px = rec.x; py = rec.y; pz = rec.z; }
		const auto snap = cmp::make_world_snapshot(world.gameHour, world.gameDaysPassed, world.weatherFormId, world.hostLocation, world.hostX, world.hostY, world.hostZ, world.hostPeerId, placeLoc, px, py, pz, isNew);
		send_reliable(it->second, &snap, sizeof(snap), "WorldSnapshot");
		for (auto& [_, other] : clients) {
			if (same_addr(other.addr, from) || !other.havePose) continue;
			send_unreliable(it->second, &other.lastPose, sizeof(other.lastPose), "PlayerPose");
			if (!other.appearance.empty()) send_blob_reliable(it->second, cmp::Msg::AppearanceChunk, other.peerId, other.appearance);
			if (!other.inventory.empty()) send_blob_reliable(it->second, cmp::Msg::InventoryChunk, other.peerId, other.inventory);
		}
		if (fakeWasOn && clients.size() >= 2) {
			send_bye_fakes(sock, clients, fakeCount);
			LOG_INFO("fake peer off (clients=%zu)", clients.size());
		}
		fakeWasOn = fakeNow;
		return;
	}
	if (type == cmp::Msg::Bye) {
		auto it = clients.find(key);
		LOG_INFO("Bye from %s peer=%u", key.c_str(), it == clients.end() ? 0 : it->second.peerId);
		if (it != clients.end()) {
			log_json_event(cfg.jsonLog, std::string("{\"event\":\"leave\",\"peer\":") + std::to_string(it->second.peerId) + ",\"key\":\"" + it->second.playerKey + "\"}");
			if (auto pit = players.find(it->second.playerKey); pit != players.end()) dirtyPlayers.insert(pit->first);
			if (it->second.peerId == world.hostPeerId) world.hostPeerId = 0;
			const auto bye = cmp::make_bye(it->second.peerId);
			for (auto& [_, other] : clients) if (!same_addr(other.addr, from)) send_reliable(other, &bye, sizeof(bye), "Bye");
			clients.erase(it); find_host(clients, world); flush_dirty(world, worldDirty, players, dirtyPlayers);
		}
		return;
	}
	if (type == cmp::Msg::NackChunk && n >= static_cast<int>(sizeof(cmp::NackChunk))) {
		cmp::NackChunk nack{};
		std::memcpy(&nack, buf, sizeof(nack));
		auto it = clients.find(key);
		if (it == clients.end()) return;
		it->second.lastSeen = t;
		const auto blobType = static_cast<cmp::Msg>(nack.blobType);
		auto source = clients.end();
		for (auto candidate = clients.begin(); candidate != clients.end(); ++candidate) {
			if (candidate->second.peerId == nack.peerId) {
				source = candidate;
				break;
			}
		}
		if (source == clients.end()) return;
		if (blobType == cmp::Msg::AppearanceChunk && !source->second.appearance.empty()) {
			send_blob_reliable(it->second, blobType, source->second.peerId, source->second.appearance);
		} else if (blobType == cmp::Msg::InventoryChunk && !source->second.inventory.empty()) {
			send_blob_reliable(it->second, blobType, source->second.peerId, source->second.inventory);
		}
		return;
	}
	if (type == cmp::Msg::WorldSnapshot && n >= static_cast<int>(sizeof(cmp::WorldSnapshot))) {
		cmp::WorldSnapshot snap{}; std::memcpy(&snap, buf, sizeof(snap)); auto it = clients.find(key); if (it == clients.end()) return; it->second.lastSeen = t;
		if (it->second.peerId == world.hostPeerId) { world.gameHour = snap.gameHour; world.gameDaysPassed = snap.gameDaysPassed; world.weatherFormId = snap.weatherFormId; world.hostLocation = snap.hostLocationFormId ? snap.hostLocationFormId : world.hostLocation; world.hostX = snap.hostX; world.hostY = snap.hostY; world.hostZ = snap.hostZ; worldDirty = true; LOG_INFO("WorldSnapshot from host peer %u hour=%.2f days=%.2f", it->second.peerId, snap.gameHour, snap.gameDaysPassed); }
		return;
	}
	if ((type == cmp::Msg::AppearanceChunk || type == cmp::Msg::InventoryChunk) && n >= static_cast<int>(sizeof(cmp::BlobChunk))) {
		cmp::BlobChunk chunk{}; std::memcpy(&chunk, buf, sizeof(chunk)); auto it = clients.find(key); if (it == clients.end()) return; it->second.lastSeen = t;
		if (!cmp::allow_rate(blobRates, key, t, 20)) { LOG_DEBUG("rate-limit %s from %s", cmp::msg_name(type).data(), key.c_str()); return; }
		std::vector<std::uint8_t> blob;
		auto& assemblies = type == cmp::Msg::AppearanceChunk ? appearAsm : invAsm;
		if (!take_blob(assemblies[it->second.peerId], buf, n, blob)) return;
		assemblies.erase(it->second.peerId);
		auto& rec = players[it->second.playerKey]; rec.key = it->second.playerKey;
		if (type == cmp::Msg::AppearanceChunk) it->second.appearance = rec.appearance = blob;
		else it->second.inventory = rec.inventory = blob;
		dirtyPlayers.insert(rec.key);
		for (auto& [_, other] : clients) if (!same_addr(other.addr, from)) send_blob_reliable(other, type, it->second.peerId, blob);
		LOG_INFO("%s peer=%u key=%s bytes=%zu", type == cmp::Msg::AppearanceChunk ? "Appearance" : "Inventory", it->second.peerId, it->second.playerKey.c_str(), blob.size());
		return;
	}
	if (type == cmp::Msg::Heartbeat && n >= static_cast<int>(sizeof(cmp::Heartbeat))) { if (auto it = clients.find(key); it != clients.end()) it->second.lastSeen = t; return; }
	if (type == cmp::Msg::Chat && n >= static_cast<int>(sizeof(cmp::Chat))) {
		cmp::Chat chat{}; std::memcpy(&chat, buf, sizeof(chat)); chat.text[sizeof(chat.text) - 1] = '\0'; auto it = clients.find(key);
		if (it != clients.end()) {
			if (!cmp::allow_rate(chatRates, key, t, 4)) { LOG_DEBUG("rate-limit Chat from %s", key.c_str()); return; }
			chat.fromPeerId = it->second.peerId;
			cmp::fill_header(chat, cmp::Msg::Chat);
			for (auto& [_, other] : clients) if (!same_addr(other.addr, from)) send_reliable(other, &chat, sizeof(chat), "Chat");
			LOG_INFO("Chat from peer=%u name=%s: %s", it->second.peerId, it->second.name.c_str(), chat.text);
		}
		return;
	}
	if (type == cmp::Msg::Kick && n >= static_cast<int>(sizeof(cmp::Kick))) {
		cmp::Kick kick{}; std::memcpy(&kick, buf, sizeof(kick)); kick.reason[sizeof(kick.reason) - 1] = '\0'; auto it = clients.find(key);
		if (it != clients.end() && it->second.peerId == world.hostPeerId) for (auto& [_, other] : clients) if (other.peerId == kick.targetPeerId) { send_reliable(other, &kick, sizeof(kick), "Kick"); LOG_INFO("Host kicked peer=%u reason=%s", kick.targetPeerId, kick.reason); break; }
		return;
	}
	if (type == cmp::Msg::Teleport && n >= static_cast<int>(sizeof(cmp::Teleport))) {
		cmp::Teleport tp{}; std::memcpy(&tp, buf, sizeof(tp)); auto it = clients.find(key);
		if (it != clients.end() && it->second.peerId == world.hostPeerId) for (auto& [_, other] : clients) if (other.peerId == tp.targetPeerId) { send_reliable(other, &tp, sizeof(tp), "Teleport"); LOG_INFO("Host teleport peer=%u to ({:.0f},{:.0f},{:.0f}) loc=%X", tp.targetPeerId, tp.x, tp.y, tp.z, tp.locationFormId); break; }
		return;
	}
	if (type == cmp::Msg::PlayerPose && n >= static_cast<int>(sizeof(cmp::PlayerPose))) {
		cmp::PlayerPose pose{}; std::memcpy(&pose, buf, sizeof(pose)); auto it = clients.find(key);
		if (it == clients.end()) { LOG_DEBUG("PlayerPose from unknown %s", key.c_str()); return; }
		if (!cmp::allow_rate(poseRates, key, t, 30)) { LOG_DEBUG("rate-limit PlayerPose from %s", key.c_str()); return; }
		it->second.lastSeen = t; ++it->second.posesIn; pose.peerId = it->second.peerId; cmp::fill_header(pose, cmp::Msg::PlayerPose); it->second.lastPose = pose; it->second.havePose = true;
		auto& rec = players[it->second.playerKey]; rec.key = it->second.playerKey; rec.havePose = true; rec.locationFormId = pose.locationFormId; rec.x = pose.x; rec.y = pose.y; rec.z = pose.z; rec.yaw = pose.yaw; dirtyPlayers.insert(rec.key);
		if (it->second.peerId == world.hostPeerId) { world.hostLocation = pose.locationFormId ? pose.locationFormId : world.hostLocation; world.hostX = pose.x; world.hostY = pose.y; world.hostZ = pose.z; worldDirty = true; }
		for (auto& [_, other] : clients) if (!same_addr(other.addr, from) && in_interest(it->second, other, cfg.interestUu)) send_to(sock, other.addr, &pose, sizeof(pose), "PlayerPose");
		return;
	}
	if (type == cmp::Msg::ActorPose && n >= static_cast<int>(sizeof(cmp::ActorPose))) {
		cmp::ActorPose pose{}; std::memcpy(&pose, buf, sizeof(pose)); auto it = clients.find(key); if (it == clients.end()) return; it->second.lastSeen = t; if (it->second.peerId != world.hostPeerId) return; cmp::fill_header(pose, cmp::Msg::ActorPose);
		for (auto& [_, other] : clients) if (!same_addr(other.addr, from) && cmp::in_interest(pose, other.lastPose, other.havePose, cfg.interestUu)) send_to(sock, other.addr, &pose, sizeof(pose), "ActorPose");
		return;
	}
	if (type == cmp::Msg::Hit && n >= static_cast<int>(sizeof(cmp::Hit))) {
		if (!cfg.pvp) return;
		cmp::Hit hit{}; std::memcpy(&hit, buf, sizeof(hit)); auto it = clients.find(key); if (it == clients.end()) return; it->second.lastSeen = t;
		if (!cmp::allow_rate(hitRates, key, t, 12) || hit.attackerPeerId != it->second.peerId || cmp::is_fake_peer(hit.targetPeerId) || hit.targetPeerId == 0 || hit.targetPeerId == hit.attackerPeerId) return;
		hit.damage = cmp::clamp_hit_damage(hit.damage); if (hit.damage <= 0.0f) return; cmp::fill_header(hit, cmp::Msg::Hit);
		for (auto& [_, other] : clients) if (other.peerId == hit.targetPeerId) send_to(sock, other.addr, &hit, sizeof(hit), "Hit");
		return;
	}
	LOG_DEBUG("unhandled msg type=%u from %s", static_cast<unsigned>(header.type), key.c_str());
}

void ServerRuntime::expire_clients(double t)
{
	for (auto it = clients.begin(); it != clients.end();) {
		if (t - it->second.lastSeen <= kClientTimeoutSec) { ++it; continue; }
		LOG_INFO("timeout %s peer=%u", it->first.c_str(), it->second.peerId);
		if (auto pit = players.find(it->second.playerKey); pit != players.end()) dirtyPlayers.insert(pit->first);
		if (it->second.peerId == world.hostPeerId) world.hostPeerId = 0;
		it = clients.erase(it);
		find_host(clients, world);
		flush_dirty(world, worldDirty, players, dirtyPlayers);
	}
}

void ServerRuntime::send_heartbeat()
{
	const auto hb = cmp::make_heartbeat(0);
	for (auto& [_, client] : clients) send_to(sock, client.addr, &hb, sizeof(hb), "Heartbeat");
}
