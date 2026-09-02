#include "pch.h"
#include "session.h"
#include "net.h"
#include "net/internal.h"
#include "appearance.h"
#include "actors.h"
#include "companions.h"
#include "ghost.h"
#include "indicators.h"
#include "modhash.h"
#include "pointer.h"
#include "presence.h"
#include "udp_win.h"

#include <atomic>
#include <iomanip>
#include <sstream>
#include <thread>

namespace cmp_net {

std::atomic<double> g_lastRecvSec{ 0.0 };

namespace {

std::thread g_heartbeatThread;
std::atomic<bool> g_heartbeatRun{ false };

void HeartbeatLoop()
{
	while (g_heartbeatRun.load(std::memory_order_relaxed)) {
		std::this_thread::sleep_for(std::chrono::seconds(5));
		auto& s = CMP_Session();
		if (!s.net.joined || s.net.myPeerId == 0) {
			continue;
		}
		const auto hb = cmp::make_heartbeat(s.net.myPeerId);
		cmp_udp_send(s.settings.host.c_str(), s.settings.port, &hb, static_cast<int>(sizeof(hb)));
	}
}

}  // namespace

void StartHeartbeat()
{
	StopHeartbeat();
	g_heartbeatRun.store(true, std::memory_order_relaxed);
	g_heartbeatThread = std::thread(HeartbeatLoop);
}

void StopHeartbeat()
{
	g_heartbeatRun.store(false, std::memory_order_relaxed);
	if (g_heartbeatThread.joinable()) {
		g_heartbeatThread.join();
	}
}

}  // namespace cmp_net

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
	const auto world = cmp_net::ReadLocalWorld();
	std::ostringstream o;
	{
		std::lock_guard lock(s.mutex);
		o << "CommonwealthMP " << F4SE::GetPluginVersion().string() << " joined=" << (s.net.joined ? "yes" : "no")
		  << " peer=" << s.net.myPeerId
		  << " host=" << (s.net.isHost ? 1 : 0)
		  << " pvp=" << ((s.menu.sessionFlags & cmp::kSessionPvpEnabled) ? "on" : "off")
		  << " interior=" << (world.interior ? 1 : 0)
		  << " loc=" << std::hex << std::uppercase << world.location << std::dec
		  << " new=" << (s.net.isNewPlayer ? "yes" : "no")
		  << " fake=" << s.net.fakePeerId
		  << " ip=" << s.settings.host << ":" << s.settings.port
		  << " key=" << s.settings.playerKey
		  << " remotes=" << s.net.latestPose.size()
		  << " ghosts=" << s.ghosts.byPeer.size()
		  << " 3D=" << ghosts3d;
		if (s.net.joined) {
			const double now = cmp_net::NowSec();
			o << " tx=" << static_cast<int>(now - s.net.lastSendPoseSec) << "s"
			  << " rx=" << static_cast<int>(now - s.net.lastRecvPoseSec) << "s"
			  << " rtt=" << static_cast<int>(s.net.measuredRttMs) << "ms";
		}
		for (const auto& [peer, name] : s.ghosts.names) {
			if (peer == s.net.myPeerId) {
				continue;
			}
			o << " [" << peer << "]" << name;
		}
		const cmp::PlayerPose* best = nullptr;
		for (const auto& [peer, pose] : s.net.latestPose) {
			if (peer == s.net.myPeerId) {
				continue;
			}
			if (s.net.fakePeerId != 0 && peer == s.net.fakePeerId) {
				best = &pose;
				break;
			}
			if (!best) {
				best = &pose;
			}
		}
		if (best) {
			o << " spd=" << static_cast<int>(best->speed)
			  << " anim=" << cmp::fake_anim_name(best->flags)
			  << " drawn=" << (cmp::has_pose_flag(best->flags, cmp::PoseFlag::Drawn) ? 1 : 0)
			  << " atk=" << (cmp::has_pose_flag(best->flags, cmp::PoseFlag::Attacking) ? 1 : 0)
			  << " rld=" << (cmp::has_pose_flag(best->flags, cmp::PoseFlag::Reloading) ? 1 : 0)
			  << " ads=" << (cmp::has_pose_flag(best->flags, cmp::PoseFlag::Sighted) ? 1 : 0)
			  << " jump=" << (cmp::has_pose_flag(best->flags, cmp::PoseFlag::Jumping) ? 1 : 0)
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
	s.menu.joinFlags = flags;
	s.net.snapshotApplied = false;
	s.net.haveSnapshot = false;
	s.net.isHost = false;
	s.net.isNewPlayer = false;
	s.lastReject.clear();

	const auto world = cmp_net::ReadLocalWorld();
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
		flags,
		CMP_ComputeModHash(),
		s.settings.password);
	if (!cmp_udp_send(s.settings.host.c_str(), s.settings.port, &hello, static_cast<int>(sizeof(hello)))) {
		s.lastStatus = "send Hello failed (bad host?)";
		CMP_Print(s.lastStatus);
		cmp_udp_shutdown();
		return false;
	}

	s.net.joined = true;
	cmp_net::StartHeartbeat();
	s.lastStatus = "hello sent, waiting Welcome";
	CMP_Print("Join IP " + s.settings.host + ":" + std::to_string(s.settings.port)
		+ " key=" + s.settings.playerKey
		+ (flags & cmp::kHelloFlagRequireHost ? " requireHost" : ""));
	REX::INFO("cmp_join host={}:{} key={} name={} loc={:X} flags={}",
		s.settings.host, s.settings.port, s.settings.playerKey, s.settings.playerName, world.location, flags);
	CMP_Presence_Invalidate();
	return true;
}

void CMP_Leave()
{
	auto& s = CMP_Session();
	if (s.net.joined) {
		const auto bye = cmp::make_bye(s.net.myPeerId);
		cmp_udp_send(s.settings.host.c_str(), s.settings.port, &bye, static_cast<int>(sizeof(bye)));
	}
	cmp_net::StopHeartbeat();
	CMP_Reliable_Reset();
	cmp_udp_shutdown();
	CMP_DespawnGhosts();
	CMP_IndicatorsClear();
	s.net.joined = false;
	s.net.myPeerId = 0;
	s.net.fakePeerId = 0;
	s.net.isHost = false;
	s.net.isNewPlayer = false;
	s.net.haveSnapshot = false;
	s.net.snapshotApplied = false;
	{
		std::lock_guard lock(s.mutex);
		s.net.incoming.clear();
		s.net.latestPose.clear();
		s.net.poseRing.clear();
		s.net.actorRing.clear();
		s.net.latestActors.clear();
		s.blobs.appearances.clear();
		s.blobs.inventories.clear();
		s.blobs.appearanceParts.clear();
		s.blobs.inventoryParts.clear();
		s.ghosts.names.clear();
		s.lastGhostNote.clear();
	}
	s.lastStatus = "disconnected";
	s.net.lastPointerHud = 0.0;
	s.blobs.lastAppearanceSend = 0.0;
	s.blobs.lastInventorySend = 0.0;
	s.net.lastSendPoseSec = 0.0;
	s.net.lastRecvPoseSec = 0.0;
	s.net.measuredRttMs = 0.0f;
	s.menu.joinFlags = 0;
	s.menu.sessionFlags = 0;
	s.menu.menuJoin = false;
	CMP_ClearHostActors();
	CMP_Presence_Invalidate();
}

void CMP_ApplyWorldSnapshot(const cmp::WorldSnapshot& snap)
{
	auto& s = CMP_Session();
	s.net.lastSnapshot = snap;
	s.net.haveSnapshot = true;
	if (s.net.snapshotApplied) {
		return;
	}
	s.net.snapshotApplied = true;

	const bool iAmHost = s.net.isHost || (s.net.myPeerId != 0 && snap.hostPeerId == s.net.myPeerId);
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
		const auto world = cmp_net::ReadLocalWorld();
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
