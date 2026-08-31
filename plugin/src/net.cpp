#include "pch.h"
#include "cmp.h"
#include "udp_win.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>
#include <sstream>

namespace {

struct LocalWorld {
	std::uint32_t location{ 0 };
	float x{ 0 };
	float y{ 0 };
	float z{ 0 };
	float yaw{ 0 };
	float days{ 0 };
	float hour{ 0 };
	std::uint32_t weather{ 0 };
	bool inWorld{ false };
	bool interior{ false };
};

LocalWorld ReadLocalWorld()
{
	LocalWorld w;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return w;
	}
	if (auto* cell = player->GetParentCell()) {
		w.inWorld = true;
		w.interior = cell->IsInterior();
		if (w.interior) {
			w.location = cell->GetFormID();
		} else if (cell->worldSpace) {
			w.location = cell->worldSpace->GetFormID();
		} else {
			w.location = cell->GetFormID();
		}
	}
	const auto pos = player->GetPosition();
	w.x = pos.x;
	w.y = pos.y;
	w.z = pos.z;
	w.yaw = player->GetHeading();
	if (auto* cal = RE::Calendar::GetSingleton()) {
		if (cal->gameDaysPassed) {
			w.days = cal->gameDaysPassed->GetValue();
		}
		if (cal->gameHour) {
			w.hour = cal->gameHour->GetValue();
		}
	}
	if (auto* sky = RE::Sky::GetSingleton()) {
		if (auto* weather = sky->currentWeather ? sky->currentWeather : sky->overrideWeather) {
			w.weather = weather->GetFormID() & 0x00FFFFFF;
		}
	}
	return w;
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
	std::vector<std::uint8_t> complete;
	{
		std::lock_guard lock(s.mutex);
		const auto st = cmp::assemble_blob_chunk(
			parts[chunk.peerId],
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
		auto it = s.ghosts.find(chunk.peerId);
		if (it != s.ghosts.end()) {
			if (const auto ptr = it->second.get()) {
				actor = ptr->As<RE::Actor>();
			}
		}
	}
	if (actor && apply) {
		apply(actor, chunk.peerId);
	}
	REX::INFO("{} assembled peer={} bytes={}", label, chunk.peerId, complete.size());
}

}  // namespace

void CMP_Print(const std::string& line)
{
	REX::INFO("{}", line);
	if (auto* log = RE::ConsoleLog::GetSingleton()) {
		log->AddString((line + "\n").c_str());
	}
}

std::string CMP_StatusText()
{
	auto& s = CMP_Session();
	const int ghosts3d = CMP_CountGhostsWith3D();
	std::ostringstream o;
	{
		std::lock_guard lock(s.mutex);
		o << "CommonwealthMP 0.5.7 joined=" << (s.joined ? "yes" : "no")
		  << " peer=" << s.myPeerId
		  << " host=" << (s.isHost ? "yes" : "no")
		  << " new=" << (s.isNewPlayer ? "yes" : "no")
		  << " fake=" << s.fakePeerId
		  << " ip=" << s.settings.host << ":" << s.settings.port
		  << " key=" << s.settings.playerKey
		  << " remotes=" << s.latestPose.size()
		  << " ghosts=" << s.ghosts.size()
		  << " 3D=" << ghosts3d;
		const cmp::PlayerPose* best = nullptr;
		for (const auto& [peer, pose] : s.latestPose) {
			if (peer == s.myPeerId) {
				continue;
			}
			if (s.fakePeerId != 0 && peer == s.fakePeerId) {
				best = &pose;
				break;
			}
			if (!best) {
				best = &pose;
			}
		}
		if (best) {
			o << " spd=" << static_cast<int>(best->speed)
			  << " drawn=" << (cmp::has_pose_flag(best->flags, cmp::PoseFlag::Drawn) ? 1 : 0)
			  << " sneak=" << (cmp::has_pose_flag(best->flags, cmp::PoseFlag::Sneak) ? 1 : 0)
			  << " sprint=" << (cmp::has_pose_flag(best->flags, cmp::PoseFlag::Sprint) ? 1 : 0);
		}
		o << " " << s.lastStatus;
		if (!s.lastReject.empty()) {
			o << " reject=" << s.lastReject;
		}
	}
	o << " | " << CMP_PointerText();
	return o.str();
}

bool CMP_Join(std::string host, std::uint16_t port, std::uint8_t flags)
{
	CMP_Leave();
	auto& s = CMP_Session();
	if (!cmp_udp_startup()) {
		s.lastStatus = "udp startup failed";
		CMP_Print(s.lastStatus);
		return false;
	}

	s.settings.host = std::move(host);
	s.settings.port = port;
	s.joinFlags = flags;
	s.snapshotApplied = false;
	s.haveSnapshot = false;
	s.isHost = false;
	s.isNewPlayer = false;
	s.lastReject.clear();

	const auto world = ReadLocalWorld();
	if (!world.inWorld) {
		s.lastStatus = "not in world (load any save first)";
		CMP_Print(s.lastStatus);
		RE::SendHUDMessage::ShowHUDMessage(s.lastStatus.c_str(), "", false, false);
		cmp_udp_shutdown();
		return false;
	}

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
		flags);
	if (!cmp_udp_send(s.settings.host.c_str(), s.settings.port, &hello, static_cast<int>(sizeof(hello)))) {
		s.lastStatus = "send Hello failed (bad host?)";
		CMP_Print(s.lastStatus);
		cmp_udp_shutdown();
		return false;
	}

	s.joined = true;
	s.lastStatus = "hello sent, waiting Welcome";
	CMP_Print("Join IP " + s.settings.host + ":" + std::to_string(s.settings.port)
		+ " key=" + s.settings.playerKey
		+ (flags & cmp::kHelloFlagRequireHost ? " requireHost" : ""));
	REX::INFO("cmp_join host={}:{} key={} name={} loc={:X} flags={}",
		s.settings.host, s.settings.port, s.settings.playerKey, s.settings.playerName, world.location, flags);
	return true;
}

void CMP_Leave()
{
	auto& s = CMP_Session();
	if (s.joined) {
		const auto bye = cmp::make_bye(s.myPeerId);
		cmp_udp_send(s.settings.host.c_str(), s.settings.port, &bye, static_cast<int>(sizeof(bye)));
	}
	cmp_udp_shutdown();
	s.joined = false;
	s.myPeerId = 0;
	s.fakePeerId = 0;
	s.isHost = false;
	s.isNewPlayer = false;
	s.haveSnapshot = false;
	s.snapshotApplied = false;
	{
		std::lock_guard lock(s.mutex);
		s.incoming.clear();
		s.latestPose.clear();
		s.appearances.clear();
		s.inventories.clear();
		s.appearanceParts.clear();
		s.inventoryParts.clear();
		s.ghostNames.clear();
		s.lastGhostNote.clear();
	}
	s.lastStatus = "disconnected";
	s.lastPointerHud = 0.0;
	s.lastAppearanceSend = 0.0;
	s.lastInventorySend = 0.0;
	s.joinFlags = 0;
	s.menuJoin = false;
}

void CMP_ApplyWorldSnapshot(const cmp::WorldSnapshot& snap)
{
	auto& s = CMP_Session();
	s.lastSnapshot = snap;
	s.haveSnapshot = true;
	if (s.snapshotApplied) {
		return;
	}
	s.snapshotApplied = true;

	const bool iAmHost = s.isHost || (s.myPeerId != 0 && snap.hostPeerId == s.myPeerId);
	if (!iAmHost) {
		if (auto* cal = RE::Calendar::GetSingleton()) {
			if (cal->gameHour) {
				cal->gameHour->value = snap.gameHour;
			}
			if (cal->gameDaysPassed) {
				cal->gameDaysPassed->value = snap.gameDaysPassed;
			}
		}
		if (snap.weatherFormId) {
			if (auto* form = CMP_ResolveForm(snap.weatherFormId, "Fallout4.esm")) {
				if (auto* weather = form->As<RE::TESWeather>()) {
					if (auto* sky = RE::Sky::GetSingleton()) {
						sky->ForceWeather(weather, true);
					}
				}
			}
		}
		REX::INFO("WorldSnapshot applied host calendar hour={:.2f} days={:.2f} weather={:X}",
			snap.gameHour, snap.gameDaysPassed, snap.weatherFormId);
	} else {
		REX::INFO("WorldSnapshot host keeps local calendar");
	}

	if (iAmHost) {
		REX::INFO("WorldSnapshot host keeps local pose");
		return;
	}

	if (snap.placeLocationFormId != 0 && snap.placeLocationFormId != cmp::kCommonwealthWorldspace) {
		const auto world = ReadLocalWorld();
		if (world.inWorld && world.location == snap.placeLocationFormId) {
			REX::INFO("WorldSnapshot keep local pose in shared cell {:X}", snap.placeLocationFormId);
			return;
		}
		REX::INFO("WorldSnapshot skip warp: place loc={:X} (menu join stays Commonwealth exterior)",
			snap.placeLocationFormId);
		return;
	}

	if (auto* player = RE::PlayerCharacter::GetSingleton()) {
		player->SetPosition(RE::NiPoint3{ snap.placeX, snap.placeY, snap.placeZ + 12.0f }, true);
		REX::INFO("WorldSnapshot warp {} to ({:.0f},{:.0f},{:.0f}) loc={:X}",
			snap.isNewPlayer ? "new player spawn" : "last pose",
			snap.placeX, snap.placeY, snap.placeZ, snap.placeLocationFormId);
	}
}

namespace {

bool g_queryPending = false;
double g_queryStart = 0.0;
SessionQueryResult g_queryResult;

double NowSec()
{
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

}  // namespace

void CMP_QueryStart(std::string host, std::uint16_t port)
{
	auto& s = CMP_Session();
	if (s.joined) {
		CMP_Leave();
	} else {
		cmp_udp_shutdown();
	}
	g_queryPending = false;
	g_queryResult = {};
	s.settings.host = std::move(host);
	s.settings.port = port;
	if (!cmp_udp_startup()) {
		g_queryResult.error = "udp startup failed";
		g_queryResult.ok = false;
		return;
	}
	const auto q = cmp::make_session_query();
	if (!cmp_udp_send(s.settings.host.c_str(), s.settings.port, &q, static_cast<int>(sizeof(q)))) {
		g_queryResult.error = "send query failed (bad host?)";
		cmp_udp_shutdown();
		return;
	}
	g_queryPending = true;
	g_queryStart = NowSec();
	s.lastStatus = "querying " + s.settings.host + ":" + std::to_string(s.settings.port);
	REX::INFO("SessionQuery {}:{}", s.settings.host, s.settings.port);
}

bool CMP_QueryPoll(SessionQueryResult& out)
{
	if (!g_queryPending) {
		if (!g_queryResult.error.empty() || g_queryResult.ok) {
			out = g_queryResult;
			return true;
		}
		out.error = "no query";
		return true;
	}

	for (int i = 0; i < 8; ++i) {
		char buf[512]{};
		const int n = cmp_udp_recv(buf, sizeof(buf));
		if (n < static_cast<int>(sizeof(cmp::Header))) {
			break;
		}
		cmp::Header header{};
		std::memcpy(&header, buf, sizeof(header));
		if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
			continue;
		}
		if (static_cast<cmp::Msg>(header.type) == cmp::Msg::SessionInfo && n >= static_cast<int>(sizeof(cmp::SessionInfo))) {
			std::memcpy(&g_queryResult.info, buf, sizeof(g_queryResult.info));
			g_queryResult.info.serverName[sizeof(g_queryResult.info.serverName) - 1] = '\0';
			g_queryResult.info.motd[sizeof(g_queryResult.info.motd) - 1] = '\0';
			g_queryPending = false;
			cmp_udp_shutdown();
			if (!g_queryResult.info.haveHost) {
				g_queryResult.ok = false;
				g_queryResult.error = "no live host on that server";
			} else if (g_queryResult.info.hostInterior || g_queryResult.info.hostLocationFormId != cmp::kCommonwealthWorldspace) {
				g_queryResult.ok = false;
				g_queryResult.error = "host is not Commonwealth exterior";
			} else {
				g_queryResult.ok = true;
				g_queryResult.error.clear();
			}
			out = g_queryResult;
			REX::INFO("SessionInfo name={} clients={}/{} haveHost={} loc={:X} host=({},{},{}) motd={} ok={}",
				g_queryResult.info.serverName,
				g_queryResult.info.clientCount,
				g_queryResult.info.maxPlayers,
				g_queryResult.info.haveHost,
				g_queryResult.info.hostLocationFormId,
				g_queryResult.info.hostX,
				g_queryResult.info.hostY,
				g_queryResult.info.hostZ,
				g_queryResult.info.motd,
				g_queryResult.ok ? 1 : 0);
			return true;
		}
	}

	if (NowSec() - g_queryStart > 2.0) {
		g_queryPending = false;
		cmp_udp_shutdown();
		g_queryResult.ok = false;
		g_queryResult.error = "server timeout (need 0.5.x server)";
		out = g_queryResult;
		REX::INFO("SessionQuery timeout");
		return true;
	}
	return false;
}

bool CMP_PlayerInCommonwealth()
{
	const auto world = ReadLocalWorld();
	return world.inWorld && !world.interior && world.location == cmp::kCommonwealthWorldspace;
}

void CMP_EnsureCommonwealthExterior()
{
	if (CMP_PlayerInCommonwealth()) {
		return;
	}
	RE::Console::ExecuteCommand("coc SanctuaryExt");
	REX::INFO("coc SanctuaryExt (menu join needs Commonwealth exterior)");
}

void CMP_NetPoll()
{
	auto& s = CMP_Session();
	if (!s.joined) {
		return;
	}

	for (int i = 0; i < 12; ++i) {
		char buf[512]{};
		const int n = cmp_udp_recv(buf, sizeof(buf));
		if (n < static_cast<int>(sizeof(cmp::Header))) {
			break;
		}

		cmp::Header header{};
		std::memcpy(&header, buf, sizeof(header));
		if (!cmp::header_ok(header, static_cast<std::size_t>(n))) {
			continue;
		}

		const auto type = static_cast<cmp::Msg>(header.type);
		if (type == cmp::Msg::Reject && n >= static_cast<int>(sizeof(cmp::Reject))) {
			cmp::Reject reject{};
			std::memcpy(&reject, buf, sizeof(reject));
			reject.message[95] = '\0';
			s.joined = false;
			s.lastReject = reject.message;
			s.lastStatus = std::string("rejected: ") + reject.message;
			CMP_Print(s.lastStatus);
			RE::SendHUDMessage::ShowHUDMessage(s.lastStatus.c_str(), "", false, false);
			cmp_udp_shutdown();
			continue;
		}

		if (type == cmp::Msg::Welcome && n >= static_cast<int>(sizeof(cmp::Welcome))) {
			cmp::Welcome welcome{};
			std::memcpy(&welcome, buf, sizeof(welcome));
			s.myPeerId = welcome.peerId;
			s.fakePeerId = welcome.fakePeerId;
			s.isNewPlayer = welcome.isNewPlayer != 0;
			s.isHost = welcome.isHost != 0;
			s.lastStatus = "welcome peer " + std::to_string(s.myPeerId);
			REX::INFO("Welcome peerId={} fake={} new={} host={}",
				s.myPeerId, s.fakePeerId, s.isNewPlayer ? 1 : 0, s.isHost ? 1 : 0);
			const auto world = ReadLocalWorld();
			const auto snap = cmp::make_world_snapshot(
				world.hour,
				world.days,
				world.weather,
				world.location,
				world.x,
				world.y,
				world.z,
				s.myPeerId,
				world.location,
				world.x,
				world.y,
				world.z,
				s.isNewPlayer);
			cmp_udp_send(s.settings.host.c_str(), s.settings.port, &snap, static_cast<int>(sizeof(snap)));
			CMP_SendAppearance(true);
			CMP_SendInventory(true);
			continue;
		}

		if (type == cmp::Msg::WorldSnapshot && n >= static_cast<int>(sizeof(cmp::WorldSnapshot))) {
			cmp::WorldSnapshot snap{};
			std::memcpy(&snap, buf, sizeof(snap));
			CMP_ApplyWorldSnapshot(snap);
			continue;
		}

		if (type == cmp::Msg::Bye && n >= static_cast<int>(sizeof(cmp::Bye))) {
			cmp::Bye bye{};
			std::memcpy(&bye, buf, sizeof(bye));
			std::lock_guard lock(s.mutex);
			s.latestPose.erase(bye.peerId);
			s.appearances.erase(bye.peerId);
			s.inventories.erase(bye.peerId);
			if (s.fakePeerId == bye.peerId) {
				s.fakePeerId = 0;
			}
			REX::INFO("Bye peer {}", bye.peerId);
			continue;
		}

		if (type == cmp::Msg::PlayerPose && n >= static_cast<int>(sizeof(cmp::PlayerPose))) {
			QueuedPose q{};
			std::memcpy(&q.pose, buf, sizeof(q.pose));
			std::lock_guard lock(s.mutex);
			s.incoming.push_back(q);
			continue;
		}

		if (type == cmp::Msg::AppearanceChunk) {
			HandleBlobChunk(buf, n, s.appearanceParts, s.appearances, CMP_ApplyGhostAppearance, "Appearance");
			continue;
		}

		if (type == cmp::Msg::InventoryChunk) {
			HandleBlobChunk(buf, n, s.inventoryParts, s.inventories, CMP_ApplyGhostInventory, "Inventory");
			continue;
		}
	}

	{
		std::lock_guard lock(s.mutex);
		for (const auto& q : s.incoming) {
			if (q.pose.peerId == s.myPeerId) {
				continue;
			}
			s.latestPose[q.pose.peerId] = q.pose;
		}
		s.incoming.clear();
	}
}

void CMP_SendLocalPose()
{
	CMP_CrashNote("pose");
	auto& s = CMP_Session();
	if (!s.joined) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	using clock = std::chrono::steady_clock;
	const double t = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	const double minDt = 1.0 / static_cast<double>(std::max(1, s.settings.poseHz));
	if (s.lastSend > 0.0 && (t - s.lastSend) < minDt) {
		return;
	}
	s.lastSend = t;

	const auto world = ReadLocalWorld();
	auto pose = cmp::make_pose(s.myPeerId, world.location, world.x, world.y, world.z, world.yaw);
	CMP_FillLocalMotion(pose);
	cmp_udp_send(s.settings.host.c_str(), s.settings.port, &pose, static_cast<int>(sizeof(pose)));
	CMP_SendAppearance(false);
	CMP_SendInventory(false);
}
