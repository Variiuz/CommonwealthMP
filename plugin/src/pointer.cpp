#include "pch.h"
#include "cmp.h"

#include <chrono>
#include <cmath>
#include <sstream>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct RemoteSnapshot {
	std::uint32_t peer{ 0 };
	cmp::PlayerPose pose{};
	bool hasGhost{ false };
	std::uint32_t youLoc{ 0 };
	std::string ghostNote;
	std::string name;
	bool joined{ false };
	std::uint32_t myPeer{ 0 };
};

const char* Compass8(float radians)
{
	float deg = radians * (180.0f / kPi);
	while (deg < 0.0f) {
		deg += 360.0f;
	}
	while (deg >= 360.0f) {
		deg -= 360.0f;
	}
	static constexpr const char* kDir[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
	const int idx = static_cast<int>(std::round(deg / 45.0f)) & 7;
	return kDir[idx];
}

float WrapPi(float a)
{
	while (a > kPi) {
		a -= 2.0f * kPi;
	}
	while (a < -kPi) {
		a += 2.0f * kPi;
	}
	return a;
}

std::uint32_t PlayerLocationForm()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return 0;
	}
	auto* cell = player->GetParentCell();
	if (!cell) {
		return 0;
	}
	if (cell->IsInterior()) {
		return cell->GetFormID();
	}
	if (cell->worldSpace) {
		return cell->worldSpace->GetFormID();
	}
	return cell->GetFormID();
}

bool CopyNearest(RemoteSnapshot& out)
{
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	out.joined = s.joined;
	out.myPeer = s.myPeerId;
	out.ghostNote = s.lastGhostNote;
	out.youLoc = PlayerLocationForm();

	const cmp::PlayerPose* best = nullptr;
	for (const auto& [peer, pose] : s.latestPose) {
		if (peer == s.myPeerId) {
			continue;
		}
		if (s.fakePeerId != 0 && peer == s.fakePeerId) {
			best = &pose;
			out.peer = peer;
			break;
		}
		if (!best) {
			best = &pose;
			out.peer = peer;
		}
	}
	if (!best) {
		return false;
	}
	out.pose = *best;
	auto git = s.ghosts.find(out.peer);
	out.hasGhost = git != s.ghosts.end() && static_cast<bool>(git->second);
	if (auto nit = s.ghostNames.find(out.peer); nit != s.ghostNames.end()) {
		out.name = nit->second;
	}
	return true;
}

std::string FormatPointer(const RemoteSnapshot& snap, bool haveRemote)
{
	if (!snap.joined) {
		return "CMP idle. Open console: cmp_join 127 0 0 1 7777";
	}
	if (!haveRemote) {
		std::ostringstream o;
		o << "CMP joined peer=" << snap.myPeer << " waiting for remotes";
		if (!snap.ghostNote.empty()) {
			o << " (" << snap.ghostNote << ")";
		}
		return o.str();
	}

	const std::string label = snap.name.empty() ? ("peer " + std::to_string(snap.peer)) : snap.name;
	const bool otherCell = snap.pose.locationFormId != 0 && snap.youLoc != 0 && snap.pose.locationFormId != snap.youLoc;
	if (otherCell) {
		std::ostringstream o;
		o << "CMP " << label << " · other cell";
		if (!snap.ghostNote.empty()) {
			o << " (" << snap.ghostNote << ")";
		}
		return o.str();
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	const auto you = player ? player->GetPosition() : RE::NiPoint3{};
	const float dx = snap.pose.x - you.x;
	const float dy = snap.pose.y - you.y;
	const float dz = snap.pose.z - you.z;
	const float distUnits = std::sqrt(dx * dx + dy * dy + dz * dz);
	const int distM = static_cast<int>(distUnits / 70.0f + 0.5f);
	const float bearing = std::atan2(dx, dy);
	const float heading = player ? player->GetHeading() : 0.0f;
	const float rel = WrapPi(bearing - heading);
	const char* look = "ahead";
	if (rel > 0.45f) {
		look = "look right";
	} else if (rel < -0.45f) {
		look = "look left";
	}

	std::ostringstream o;
	o << "CMP " << label
	  << "  " << distM << "m  " << Compass8(bearing)
	  << "  " << look
	  << (snap.hasGhost ? "  ghost=yes" : "  ghost=no");
	if (!snap.hasGhost && !snap.ghostNote.empty()) {
		o << "  " << snap.ghostNote;
	}
	return o.str();
}

}  // namespace

std::string CMP_PointerText()
{
	RemoteSnapshot snap;
	const bool have = CopyNearest(snap);
	return FormatPointer(snap, have);
}

void CMP_PointerTick()
{
	auto& s = CMP_Session();
	if (!s.settings.pointerHud || !s.joined) {
		return;
	}

	using clock = std::chrono::steady_clock;
	const double t = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	const double minDt = static_cast<double>(s.settings.pointerSeconds);
	if (s.lastPointerHud > 0.0 && (t - s.lastPointerHud) < minDt) {
		return;
	}
	s.lastPointerHud = t;

	const auto line = CMP_PointerText();
	RE::SendHUDMessage::ShowHUDMessage(line.c_str(), "", false, false);
}

bool CMP_GotoNearest()
{
	RemoteSnapshot snap;
	if (!CopyNearest(snap)) {
		CMP_Print("cmp_goto: no remote pose (join first, wait for fake/friend)");
		return false;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		CMP_Print("cmp_goto: no player");
		return false;
	}

	const auto you = player->GetPosition();
	float dx = you.x - snap.pose.x;
	float dy = you.y - snap.pose.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	constexpr float standOff = 220.0f;
	RE::NiPoint3 dest{ snap.pose.x, snap.pose.y, snap.pose.z + 12.0f };
	if (len > 1.0f) {
		dest.x = snap.pose.x + dx / len * standOff;
		dest.y = snap.pose.y + dy / len * standOff;
	} else {
		dest.x += standOff;
	}

	player->SetPosition(dest, true);
	player->SetHeading(std::atan2(snap.pose.x - dest.x, snap.pose.y - dest.y));

	std::ostringstream o;
	o << "cmp_goto: warped to peer " << snap.peer
	  << " at " << static_cast<int>(dest.x) << "," << static_cast<int>(dest.y) << "," << static_cast<int>(dest.z)
	  << "  " << CMP_PointerText();
	const auto msg = o.str();
	CMP_Print(msg);
	RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), "", false, false);
	return true;
}
