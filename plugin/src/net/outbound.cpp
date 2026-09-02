#include "pch.h"
#include "session.h"
#include "net.h"
#include "net/internal.h"
#include "ui_pose.h"
#include "udp_win.h"
#include "appearance.h"
#include "actors.h"
#include "crash.h"
#include "puppet.h"

#include <algorithm>

void CMP_SendChat(const char* text)
{
	auto& s = CMP_Session();
	if (!s.net.joined || s.net.myPeerId == 0 || !text || !text[0]) {
		return;
	}
	const auto chat = cmp::make_chat(s.net.myPeerId, text);
	CMP_Net_Send(&chat, static_cast<int>(sizeof(chat)));
}

void CMP_SendKick(std::uint32_t targetPeerId, const char* reason)
{
	auto& s = CMP_Session();
	if (!s.net.joined || s.net.myPeerId == 0 || targetPeerId == 0) {
		return;
	}
	const auto kick = cmp::make_kick(targetPeerId, reason ? reason : "");
	CMP_Net_Send(&kick, static_cast<int>(sizeof(kick)));
}

void CMP_SendTeleport(std::uint32_t targetPeerId)
{
	auto& s = CMP_Session();
	if (!s.net.joined || s.net.myPeerId == 0 || targetPeerId == 0) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}
	const auto pos = player->GetPosition();
	std::uint32_t location = 0;
	if (auto* cell = player->GetParentCell()) {
		if (cell->IsInterior()) {
			location = cell->GetFormID();
		} else if (cell->worldSpace) {
			location = cell->worldSpace->GetFormID();
		}
	}
	const auto tp = cmp::make_teleport(targetPeerId, pos.x, pos.y, pos.z, location);
	CMP_Net_Send(&tp, static_cast<int>(sizeof(tp)));
}

void CMP_SendLocalPose()
{
	CMP_CrashNote("pose");
	auto& s = CMP_Session();
	if (!s.net.joined || !s.net.udpBound) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	const double t = cmp_net::NowSec();
	const double minDt = 1.0 / static_cast<double>(std::max(1, s.settings.poseHz));
	if (s.net.lastSend > 0.0 && (t - s.net.lastSend) < minDt) {
		return;
	}
	s.net.lastSend = t;
	s.net.lastSendPoseSec = t;

	const auto world = cmp_net::ReadLocalWorld();
	s.presence.interior = world.interior;
	s.presence.worldspace = world.location;
	auto pose = cmp::make_pose(s.net.myPeerId, world.location, world.x, world.y, world.z, world.yaw);
	CMP_FillLocalMotion(pose);
	CMP_Net_Send(&pose, static_cast<int>(sizeof(pose)));
	CMP_SendHostActors();
	CMP_SendAppearance(false);
	CMP_SendInventory(false);
}
