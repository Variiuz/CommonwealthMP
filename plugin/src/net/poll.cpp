#include "pch.h"
#include "session.h"
#include "net.h"
#include "net/internal.h"
#include "appearance.h"
#include "actors.h"
#include "combat.h"
#include "companions.h"
#include "ghost.h"
#include "modhash.h"
#include "presence.h"
#include "puppet.h"
#include "udp_win.h"
#include "net/motion_interp.h"

#include <span>

namespace {

void ExpireBlobAssemblies(std::unordered_map<std::uint32_t, cmp::BlobAssembly>& parts, double now, double ttlSec)
{
	for (auto it = parts.begin(); it != parts.end();) {
		if (it->second.startedSec > 0.0 && now - it->second.startedSec > ttlSec) {
			it = parts.erase(it);
		} else {
			++it;
		}
	}
}

void HandleBlobChunk(
	const char* buf,
	int n,
	std::unordered_map<std::uint32_t, cmp::BlobAssembly>& parts,
	std::unordered_map<std::uint32_t, std::vector<std::uint8_t>>& dest,
	void (*apply)(RE::Actor*, std::uint32_t),
	const char* label)
{
	if (n < static_cast<int>(sizeof(cmp::BlobChunk))) {
		return;
	}
	cmp::BlobChunk chunk{};
	std::memcpy(&chunk, buf, sizeof(chunk));

	auto& s = CMP_Session();
	const double now = cmp_net::NowSec();
	std::vector<std::uint8_t> complete;
	{
		std::lock_guard lock(s.mutex);
		auto& asmbl = parts[chunk.peerId];
		if (asmbl.startedSec <= 0.0) {
			asmbl.startedSec = now;
		}
		const auto st = cmp::assemble_blob_chunk(
			asmbl,
			std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(buf), static_cast<std::size_t>(n)),
			complete);
		if (st != cmp::AssembleStatus::Complete) {
			return;
		}
		dest[chunk.peerId] = complete;
		parts.erase(chunk.peerId);
	}

	RE::Actor* actor = nullptr;
	{
		std::lock_guard lock(s.mutex);
		auto it = s.ghosts.byPeer.find(chunk.peerId);
		if (it != s.ghosts.byPeer.end()) {
			if (const auto ptr = it->second.get()) {
				actor = ptr->As<RE::Actor>();
			}
		}
	}
	if (actor && apply) {
		apply(actor, chunk.peerId);
		CMP_ReapplyGhostPuppet(chunk.peerId);
	}
	REX::INFO("{} assembled peer={} bytes={}", label, chunk.peerId, complete.size());
}

std::string PeerDisplayName(std::uint32_t peerId)
{
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	if (auto it = s.ghosts.names.find(peerId); it != s.ghosts.names.end() && !it->second.empty()) {
		return it->second;
	}
	return "peer " + std::to_string(peerId);
}

void HandleMessage(const char* buf, int n)
{
	auto& s = CMP_Session();
	cmp::Header header{};
	std::memcpy(&header, buf, sizeof(header));
	if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
		return;
	}

	const auto type = static_cast<cmp::Msg>(header.type);
	cmp_net::g_lastRecvSec.store(cmp_net::NowSec(), std::memory_order_relaxed);

	if (type == cmp::Msg::Reject && n >= static_cast<int>(sizeof(cmp::Reject))) {
		cmp::Reject reject{};
		std::memcpy(&reject, buf, sizeof(reject));
		reject.message[95] = '\0';
		s.lastReject = reject.message;
		s.lastStatus = std::string("rejected: ") + reject.message;
		CMP_Print(s.lastStatus);
		RE::SendHUDMessage::ShowHUDMessage(s.lastStatus.c_str(), "", false, false);
		CMP_Leave();
		return;
	}

	if (type == cmp::Msg::Welcome && n >= static_cast<int>(sizeof(cmp::Welcome))) {
		cmp::Welcome welcome{};
		std::memcpy(&welcome, buf, sizeof(welcome));
		s.net.myPeerId = welcome.peerId;
		s.net.isNewPlayer = welcome.isNewPlayer != 0;
		s.net.isHost = welcome.isHost != 0;
		s.menu.sessionFlags = welcome.sessionFlags;
		s.net.udpToken = welcome.udpToken;
		s.lastStatus = "welcome peer " + std::to_string(s.net.myPeerId);
		REX::INFO("Welcome peerId={} new={} host={} token={} sessionFlags={:X}",
			s.net.myPeerId, s.net.isNewPlayer ? 1 : 0, s.net.isHost ? 1 : 0,
			s.net.udpToken, s.menu.sessionFlags);
		CMP_Print(s.net.isHost ? "Connected as host" : "Connected as guest");
		RE::SendHUDMessage::ShowHUDMessage(
			"Quests and loot are per-save until host-world ships.",
			"",
			false,
			false);
		CMP_DismissCompanionsOnJoin();
		if (s.presence.serverName.empty()) {
			s.presence.serverName = "CMP";
		}
		CMP_Presence_Invalidate();
		const auto bind = cmp::make_udp_bind(s.net.myPeerId, s.net.udpToken);
		if (cmp_net_udp_send(s.settings.host.c_str(), s.settings.port, &bind, static_cast<int>(sizeof(bind)))) {
			s.net.udpBound = true;
		}
		const auto world = cmp_net::ReadLocalWorld();
		const auto snap = cmp::make_world_snapshot(
			world.hour,
			world.days,
			world.weather,
			world.location,
			world.x,
			world.y,
			world.z,
			s.net.myPeerId,
			world.location,
			world.x,
			world.y,
			world.z,
			s.net.isNewPlayer);
		CMP_Net_Send(&snap, static_cast<int>(sizeof(snap)));
		CMP_SendAppearance(true);
		s.blobs.pendingInventoryForce = true;
		s.blobs.pendingInventoryAt = cmp_net::NowSec() + 0.15;
		return;
	}

	if (type == cmp::Msg::WorldSnapshot && n >= static_cast<int>(sizeof(cmp::WorldSnapshot))) {
		cmp::WorldSnapshot snap{};
		std::memcpy(&snap, buf, sizeof(snap));
		CMP_ApplyWorldSnapshot(snap);
		return;
	}

	if (type == cmp::Msg::Bye && n >= static_cast<int>(sizeof(cmp::Bye))) {
		cmp::Bye bye{};
		std::memcpy(&bye, buf, sizeof(bye));
		if (bye.peerId == s.net.myPeerId) {
			CMP_Print("Disconnected by server");
			CMP_Leave();
			return;
		}
		std::string leftName;
		{
			std::lock_guard lock(s.mutex);
			leftName = s.ghosts.names[bye.peerId];
			s.net.latestPose.erase(bye.peerId);
			s.net.poseRing.erase(bye.peerId);
			s.blobs.appearances.erase(bye.peerId);
			s.blobs.inventories.erase(bye.peerId);
			s.ghosts.names.erase(bye.peerId);
		}
		CMP_Print(leftName.empty() ? ("Peer " + std::to_string(bye.peerId) + " left") : (leftName + " left"));
		REX::INFO("Bye peer {}", bye.peerId);
		return;
	}

	if (type == cmp::Msg::PlayerPose && n >= static_cast<int>(sizeof(cmp::PlayerPose))) {
		QueuedPose q{};
		std::memcpy(&q.pose, buf, sizeof(q.pose));
		q.recvSec = cmp_net::NowSec();
		std::lock_guard lock(s.mutex);
		s.net.incoming.push_back(q);
		s.net.lastRecvPoseSec = q.recvSec;
		return;
	}

	if (type == cmp::Msg::ActorPose && n >= static_cast<int>(sizeof(cmp::ActorPose))) {
		cmp::ActorPose pose{};
		std::memcpy(&pose, buf, sizeof(pose));
		CMP_OnActorPose(pose);
		return;
	}

	if (type == cmp::Msg::Hit && n >= static_cast<int>(sizeof(cmp::Hit))) {
		cmp::Hit hit{};
		std::memcpy(&hit, buf, sizeof(hit));
		CMP_OnHit(hit);
		return;
	}

	if (type == cmp::Msg::AppearanceChunk) {
		HandleBlobChunk(buf, n, s.blobs.appearanceParts, s.blobs.appearances, CMP_ApplyGhostAppearance, "appearance");
		return;
	}

	if (type == cmp::Msg::InventoryChunk) {
		HandleBlobChunk(buf, n, s.blobs.inventoryParts, s.blobs.inventories, CMP_ApplyGhostInventory, "Inventory");
		return;
	}
	if (type == cmp::Msg::Heartbeat && n >= static_cast<int>(sizeof(cmp::Heartbeat))) {
		cmp::Heartbeat hb{};
		std::memcpy(&hb, buf, sizeof(hb));
		if (hb.clientStampMs != 0) {
			const auto nowMs = static_cast<std::uint32_t>(cmp_net::NowSec() * 1000.0);
			const float rtt = static_cast<float>(nowMs - hb.clientStampMs);
			if (rtt >= 0.0f && rtt < 5000.0f) {
				if (s.net.measuredRttMs <= 0.0f) {
					s.net.measuredRttMs = rtt;
				} else {
					s.net.measuredRttMs = s.net.measuredRttMs * 0.8f + rtt * 0.2f;
				}
			}
		}
		return;
	}
	if (type == cmp::Msg::SessionRules && n >= static_cast<int>(sizeof(cmp::SessionRules))) {
		cmp::SessionRules rules{};
		std::memcpy(&rules, buf, sizeof(rules));
		const auto oldFlags = s.menu.sessionFlags;
		s.menu.sessionFlags = rules.sessionFlags;
		REX::INFO("SessionRules flags={:X}", s.menu.sessionFlags);
		const bool pvpOn = (s.menu.sessionFlags & cmp::kSessionPvpEnabled) != 0;
		const bool wasPvpOn = (oldFlags & cmp::kSessionPvpEnabled) != 0;
		if (pvpOn != wasPvpOn) {
			RE::SendHUDMessage::ShowHUDMessage(pvpOn ? "PvP enabled" : "PvP disabled", "", false, false);
		}
		return;
	}
	if (type == cmp::Msg::Chat && n >= static_cast<int>(sizeof(cmp::Chat))) {
		cmp::Chat chat{};
		std::memcpy(&chat, buf, sizeof(chat));
		chat.text[sizeof(chat.text) - 1] = '\0';
		std::string from = chat.fromPeerId == 0 ? "Server" : PeerDisplayName(chat.fromPeerId);
		std::string line = "[" + from + "] " + chat.text;
		CMP_Print(line);
		{
			std::lock_guard lock(s.overlay.chatMutex);
			s.overlay.chatHistory.push_back(line);
			if (s.overlay.chatHistory.size() > 200) {
				s.overlay.chatHistory.erase(s.overlay.chatHistory.begin());
			}
		}
		return;
	}
	if (type == cmp::Msg::Kick && n >= static_cast<int>(sizeof(cmp::Kick))) {
		cmp::Kick kick{};
		std::memcpy(&kick, buf, sizeof(kick));
		kick.reason[sizeof(kick.reason) - 1] = '\0';
		CMP_Print(std::string("Kicked: ") + kick.reason);
		CMP_Leave();
		return;
	}
	if (type == cmp::Msg::Teleport && n >= static_cast<int>(sizeof(cmp::Teleport))) {
		cmp::Teleport tp{};
		std::memcpy(&tp, buf, sizeof(tp));
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && tp.targetPeerId == s.net.myPeerId) {
			player->SetPosition(RE::NiPoint3{ tp.x, tp.y, tp.z + 8.0f }, true);
			CMP_Print("Teleported by host");
			REX::INFO("Teleport to ({:.0f},{:.0f},{:.0f}) loc={:X}", tp.x, tp.y, tp.z, tp.locationFormId);
		}
	}
}

}  // namespace

void CMP_NetPoll()
{
	auto& s = CMP_Session();
	if (!s.net.joined) {
		return;
	}

	if (!cmp_net_tcp_connected()) {
		CMP_Print("TCP connection lost");
		CMP_Leave();
		return;
	}

	const double now = cmp_net::NowSec();
	if (s.net.myPeerId == 0 && s.net.joinSentSec > 0.0 && now - s.net.joinSentSec > 1.5
		&& now - s.net.lastHelloRetrySec > 1.5) {
		s.net.lastHelloRetrySec = now;
		const auto world = cmp_net::ReadLocalWorld();
		if (world.inWorld) {
			const auto hello = cmp::make_hello(
				s.settings.playerName,
				s.settings.playerKey,
				true,
				world.location,
				world.days,
				world.hour,
				world.weather,
				world.x,
				world.y,
				world.z,
				world.interior,
				s.menu.joinFlags,
				CMP_ComputeModHash(),
				s.settings.password);
			cmp_net_tcp_send(&hello, static_cast<int>(sizeof(hello)));
			REX::INFO("Hello retry host={}:{}", s.settings.host, s.settings.port);
		}
	}
	if (s.net.myPeerId != 0 && s.net.udpToken != 0 && !s.net.udpBound) {
		const auto bind = cmp::make_udp_bind(s.net.myPeerId, s.net.udpToken);
		if (cmp_net_udp_send(s.settings.host.c_str(), s.settings.port, &bind, static_cast<int>(sizeof(bind)))) {
			s.net.udpBound = true;
		}
	}
	if (s.blobs.pendingInventoryForce && now >= s.blobs.pendingInventoryAt) {
		s.blobs.pendingInventoryForce = false;
		CMP_SendInventory(true);
	}

	const double ttlSec = std::max(1, s.settings.blobAssembleTtlMs) / 1000.0;
	{
		std::lock_guard lock(s.mutex);
		ExpireBlobAssemblies(s.blobs.appearanceParts, now, ttlSec);
		ExpireBlobAssemblies(s.blobs.inventoryParts, now, ttlSec);
	}

	for (int i = 0; i < 16; ++i) {
		alignas(8) char buf[65536]{};
		const int n = cmp_net_tcp_recv_frame(buf, sizeof(buf));
		if (n == -2) {
			CMP_Print("TCP connection closed");
			CMP_Leave();
			return;
		}
		if (n < static_cast<int>(sizeof(cmp::Header))) {
			break;
		}
		HandleMessage(buf, n);
		if (!s.net.joined) {
			return;
		}
	}

	for (int i = 0; i < 16; ++i) {
		char buf[512]{};
		const int n = cmp_net_udp_recv(buf, sizeof(buf));
		if (n < static_cast<int>(sizeof(cmp::Header))) {
			break;
		}
		HandleMessage(buf, n);
		if (!s.net.joined) {
			return;
		}
	}

	if (s.net.joined && cmp_net::g_lastRecvSec.load() > 0.0 && cmp_net::NowSec() - cmp_net::g_lastRecvSec.load() > 15.0) {
		CMP_Print("Server timed out (no packets for 15s)");
		CMP_Leave();
		return;
	}

	{
		std::lock_guard lock(s.mutex);
		for (const auto& q : s.net.incoming) {
			if (q.pose.peerId == s.net.myPeerId) {
				continue;
			}
			s.net.latestPose[q.pose.peerId] = q.pose;
			s.net.poseRing[q.pose.peerId].push(cmp_motion::FromPlayerPose(q.pose, q.recvSec));
		}
		s.net.incoming.clear();
	}
}
