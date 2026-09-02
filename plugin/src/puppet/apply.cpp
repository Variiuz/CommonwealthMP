#include "pch.h"
#include "puppet.h"
#include "ghost.h"
#include "crash.h"
#include "puppet/internal.h"

#include <cmath>

namespace cmp_puppet {

std::unordered_map<std::uint32_t, PuppetPrev> g_prev;
std::unordered_set<std::uint32_t> g_graphNotReadyLogged;
std::unordered_set<std::string> g_failedEvents;
std::unordered_set<std::string> g_failedVars;
bool g_loggedGraph{ false };

float WrapDeg(float d)
{
	while (d > 180.0f) {
		d -= 360.0f;
	}
	while (d < -180.0f) {
		d += 360.0f;
	}
	return d;
}

float WrapRad(float a)
{
	while (a > kPi) {
		a -= 2.0f * kPi;
	}
	while (a < -kPi) {
		a += 2.0f * kPi;
	}
	return a;
}

void FireEvent(RE::IAnimationGraphManagerHolder* holder, const char* name)
{
	if (!holder || !name || !name[0]) {
		return;
	}
	if (holder->NotifyAnimationGraphImpl(RE::BSFixedString(name))) {
		return;
	}
	if (g_failedEvents.insert(name).second) {
		REX::WARN("NotifyAnimationGraph {} failed", name);
	}
}

void SetBoolVar(RE::Actor* actor, const char* name, bool value)
{
	if (!actor->SetGraphVariableBool(RE::BSFixedString(name), value) && g_failedVars.insert(name).second) {
		REX::WARN("SetGraphVariableBool {} failed", name);
	}
}

void SetIntVar(RE::Actor* actor, const char* name, int value)
{
	if (!actor->SetGraphVariableInt(RE::BSFixedString(name), value) && g_failedVars.insert(name).second) {
		REX::WARN("SetGraphVariableInt {} failed", name);
	}
}

void SetFloatVar(RE::Actor* actor, const char* name, float value)
{
	if (!actor->SetGraphVariableFloat(RE::BSFixedString(name), value) && g_failedVars.insert(name).second) {
		REX::WARN("SetGraphVariableFloat {} failed", name);
	}
}

bool GraphReady(RE::Actor* actor)
{
	if (!actor || !actor->Get3D() || !actor->currentProcess || !actor->currentProcess->middleHigh) {
		return false;
	}
	RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
	return actor->GetAnimationGraphManagerImpl(mgr) && mgr;
}

int WeaponTypeFor(RE::Actor* actor)
{
	if (!actor || !actor->biped) {
		return 0;
	}
	auto* form = actor->biped->object[static_cast<int>(RE::BIPED_OBJECT::kRightHand)].parent.object;
	auto* weap = form ? form->As<RE::TESObjectWEAP>() : nullptr;
	if (!weap) {
		return static_cast<int>(RE::WEAPON_TYPE::kHandToHand);
	}
	static constexpr RE::WEAPON_TYPE kOrder[] = {
		RE::WEAPON_TYPE::kHandToHand,
		RE::WEAPON_TYPE::kOneHandSword,
		RE::WEAPON_TYPE::kOneHandDagger,
		RE::WEAPON_TYPE::kOneHandAxe,
		RE::WEAPON_TYPE::kOneHandMace,
		RE::WEAPON_TYPE::kTwoHandSword,
		RE::WEAPON_TYPE::kTwoHandAxe,
		RE::WEAPON_TYPE::kBow,
		RE::WEAPON_TYPE::kStaff,
		RE::WEAPON_TYPE::kGun,
		RE::WEAPON_TYPE::kGrenade,
		RE::WEAPON_TYPE::kMine,
	};
	const auto& weaponType = weap->weaponData.type;
	for (auto wt : kOrder) {
		if (weaponType.any(wt)) {
			return static_cast<int>(wt);
		}
	}
	return static_cast<int>(RE::WEAPON_TYPE::kHandToHand);
}

void SetDumpVars(
	RE::Actor* actor,
	float pitch,
	float yaw,
	float turnDelta,
	float speed,
	float vx,
	float vy,
	std::uint32_t flags,
	bool moving,
	bool inMenu)
{
	const float useSpeed = inMenu ? 0.0f : speed;
	SetFloatVar(actor, "Speed", useSpeed);
	SetFloatVar(actor, "SpeedSampled", useSpeed);
	SetFloatVar(actor, "Pitch", pitch);
	SetFloatVar(actor, "TurnDelta", turnDelta);
	float dir = 0.0f;
	if (moving && !inMenu) {
		const float moveYaw = std::atan2(vx, vy);
		dir = WrapDeg((moveYaw - yaw) * (180.0f / kPi));
	}
	SetFloatVar(actor, "Direction", dir);
	const bool sprint = cmp::has_pose_flag(flags, cmp::PoseFlag::Sprint);
	const bool slowWalk = cmp::has_pose_flag(flags, cmp::PoseFlag::SlowWalk);
	SetBoolVar(actor, "IsSneaking", cmp::has_pose_flag(flags, cmp::PoseFlag::Sneak));
	SetBoolVar(actor, "IsSprinting", sprint);
	SetBoolVar(actor, "bInJumpState", cmp::has_pose_flag(flags, cmp::PoseFlag::Jumping));
	SetBoolVar(actor, "IsAttacking", cmp::has_pose_flag(flags, cmp::PoseFlag::Attacking));
	SetBoolVar(actor, "bWantGait", sprint);
	SetIntVar(actor, "iWantGait", sprint ? 1 : (slowWalk ? 0 : 1));
	SetIntVar(actor, "iSyncSprintState", sprint ? 1 : 0);
	SetIntVar(actor, "iSyncIdleLocomotion", moving && !inMenu ? 1 : 0);
	SetBoolVar(actor, "bMotionDriven", moving && !inMenu);
	SetBoolVar(actor, "bAnimationDriven", true);
	SetBoolVar(actor, "bAllowRotation", std::fabs(turnDelta) > 0.5f);
	const int weapType = WeaponTypeFor(actor);
	SetIntVar(actor, "EquippedWeaponType", weapType);
	SetIntVar(actor, "iRightHandType", weapType);
}

void EdgeFlag(
	RE::Actor* actor,
	bool had,
	bool now,
	const char* onEvent,
	const char* offEvent,
	const char* onAlt,
	const char* offAlt)
{
	if (had == now) {
		return;
	}
	if (now) {
		FireEvent(actor, onEvent);
		if (onAlt) {
			FireEvent(actor, onAlt);
		}
	} else {
		FireEvent(actor, offEvent);
		if (offAlt) {
			FireEvent(actor, offAlt);
		}
	}
}

void ApplyCombatFlags(
	RE::Actor* actor,
	std::uint32_t prevFlags,
	std::uint32_t flags,
	bool hadPrev,
	bool isGhost)
{
	const bool nowDrawn = cmp::has_pose_flag(flags, cmp::PoseFlag::Drawn);
	const bool wasDrawn = cmp::has_pose_flag(prevFlags, cmp::PoseFlag::Drawn);
	if (nowDrawn && !actor->GetWeaponMagicDrawn()) {
		actor->DrawWeaponMagicHands(true);
	} else if (!nowDrawn && actor->GetWeaponMagicDrawn()) {
		actor->DrawWeaponMagicHands(false);
	}
	if (!hadPrev || wasDrawn != nowDrawn) {
		if (nowDrawn) {
			FireEvent(actor, "weaponDraw");
		} else if (hadPrev) {
			FireEvent(actor, "weaponSheathe");
		}
	}

	const bool nowAttack = cmp::has_pose_flag(flags, cmp::PoseFlag::Attacking);
	const bool wasAttack = cmp::has_pose_flag(prevFlags, cmp::PoseFlag::Attacking);
	if (!hadPrev) {
		if (nowAttack && !isGhost) {
			FireEvent(actor, "attackStart");
		}
		if (cmp::has_pose_flag(flags, cmp::PoseFlag::Reloading)) {
			FireEvent(actor, "reloadStart");
			FireEvent(actor, "reloadState");
		}
		if (cmp::has_pose_flag(flags, cmp::PoseFlag::Sighted)) {
			FireEvent(actor, "sightedStart");
			FireEvent(actor, "SightedStart");
		}
		if (cmp::has_pose_flag(flags, cmp::PoseFlag::Jumping)) {
			FireEvent(actor, "JumpStandingStart");
		}
		if (cmp::has_pose_flag(flags, cmp::PoseFlag::Pipboy)) {
			FireEvent(actor, "pipboyIdle");
		}
		return;
	}

	if (!isGhost) {
		EdgeFlag(actor, wasAttack, nowAttack, "attackStart", "attackStop");
	}
	EdgeFlag(
		actor,
		cmp::has_pose_flag(prevFlags, cmp::PoseFlag::Reloading),
		cmp::has_pose_flag(flags, cmp::PoseFlag::Reloading),
		"reloadStart",
		"reloadStop",
		"reloadState",
		nullptr);
	EdgeFlag(
		actor,
		cmp::has_pose_flag(prevFlags, cmp::PoseFlag::Sighted),
		cmp::has_pose_flag(flags, cmp::PoseFlag::Sighted),
		"sightedStart",
		"sightedRelease",
		"SightedStart",
		"sightedEnd");
	EdgeFlag(
		actor,
		cmp::has_pose_flag(prevFlags, cmp::PoseFlag::Jumping),
		cmp::has_pose_flag(flags, cmp::PoseFlag::Jumping),
		"JumpStandingStart",
		"JumpLand");
	EdgeFlag(
		actor,
		cmp::has_pose_flag(prevFlags, cmp::PoseFlag::Pipboy),
		cmp::has_pose_flag(flags, cmp::PoseFlag::Pipboy),
		"pipboyIdle",
		"pipboyLower");
}

void RetryStickyFlags(RE::Actor* actor, std::uint32_t flags, PuppetPrev& prev)
{
	++prev.stickyTicks;
	if (prev.stickyTicks < kStickyRetryTicks) {
		return;
	}
	prev.stickyTicks = 0;

	const bool wantDrawn = cmp::has_pose_flag(flags, cmp::PoseFlag::Drawn);
	if (wantDrawn && !actor->GetWeaponMagicDrawn()) {
		actor->DrawWeaponMagicHands(true);
		FireEvent(actor, "weaponDraw");
	} else if (!wantDrawn && actor->GetWeaponMagicDrawn()) {
		actor->DrawWeaponMagicHands(false);
		FireEvent(actor, "weaponSheathe");
	}

	if (cmp::has_pose_flag(flags, cmp::PoseFlag::Sneak)) {
		SetBoolVar(actor, "IsSneaking", true);
		FireEvent(actor, "sneakStart");
	}
	if (cmp::has_pose_flag(flags, cmp::PoseFlag::Jumping)) {
		SetBoolVar(actor, "bInJumpState", true);
		FireEvent(actor, "JumpStandingStart");
	}
}

}  // namespace cmp_puppet

using namespace cmp_puppet;

void CMP_ApplyGhostPuppet(
	RE::Actor* actor,
	std::uint32_t id,
	float pitch,
	float yaw,
	float speed,
	float vx,
	float vy,
	std::uint32_t flags)
{
	CMP_CrashNote("puppet");
	if (!GraphReady(actor)) {
		if (g_graphNotReadyLogged.insert(id).second) {
			REX::WARN("Puppet graph not ready id={}", id);
		}
		return;
	}
	g_graphNotReadyLogged.erase(id);

	const bool isGhost = CMP_PeerForGhost(actor) != 0;
	const bool inMenu = cmp::has_pose_flag(flags, cmp::PoseFlag::Pipboy) || cmp::has_pose_flag(flags, cmp::PoseFlag::Menu);

	actor->data.angle.x = pitch;
	auto& prev = g_prev[id];
	const float turnDelta = prev.have ? WrapDeg((yaw - prev.yaw) * (180.0f / kPi)) : 0.0f;
	prev.yaw = yaw;

	if (cmp::has_pose_flag(flags, cmp::PoseFlag::Dead)) {
		SetDumpVars(actor, pitch, yaw, 0.0f, 0.0f, 0.0f, 0.0f, flags, false, true);
		ApplyCombatFlags(actor, prev.flags, flags, prev.have, isGhost);
		prev.flags = flags;
		prev.moving = false;
		prev.have = true;
		return;
	}
	const bool moving = !inMenu && speed > kMoveSpeedEps;
	SetDumpVars(actor, pitch, yaw, turnDelta, speed, vx, vy, flags, moving, inMenu);

	if (!prev.have || prev.moving != moving) {
		FireEvent(actor, moving ? "MoveStart" : "MoveStop");
		if (moving) {
			FireEvent(actor, "walkStart");
		}
	}

	if (!prev.have) {
		if (cmp::has_pose_flag(flags, cmp::PoseFlag::Sneak)) {
			FireEvent(actor, "sneakStart");
		}
		if (cmp::has_pose_flag(flags, cmp::PoseFlag::Sprint)) {
			FireEvent(actor, "sprintStart");
		}
	} else {
		const bool wasSneak = cmp::has_pose_flag(prev.flags, cmp::PoseFlag::Sneak);
		const bool nowSneak = cmp::has_pose_flag(flags, cmp::PoseFlag::Sneak);
		if (wasSneak != nowSneak) {
			FireEvent(actor, nowSneak ? "sneakStart" : "sneakStop");
		}
		const bool wasSprint = cmp::has_pose_flag(prev.flags, cmp::PoseFlag::Sprint);
		const bool nowSprint = cmp::has_pose_flag(flags, cmp::PoseFlag::Sprint);
		if (wasSprint != nowSprint) {
			FireEvent(actor, nowSprint ? "sprintStart" : "sprintStop");
		}
	}

	ApplyCombatFlags(actor, prev.flags, flags, prev.have, isGhost);
	if (prev.have) {
		RetryStickyFlags(actor, flags, prev);
	} else {
		prev.stickyTicks = 0;
	}

	prev.flags = flags;
	prev.moving = moving;
	prev.have = true;

	if (!g_loggedGraph) {
		g_loggedGraph = true;
		REX::INFO("Puppet first apply id={} spd={:.1f} flags={:X} drawn={}",
			id, speed, flags,
			cmp::has_pose_flag(flags, cmp::PoseFlag::Drawn) ? 1 : 0);
	}
}

void CMP_ApplyGhostPuppet(RE::Actor* actor, const cmp::PlayerPose& pose)
{
	CMP_ApplyGhostPuppet(actor, pose.peerId, pose.pitch, pose.yaw, pose.speed, pose.vx, pose.vy, pose.flags);
}

void CMP_ResetGhostPuppet(std::uint32_t peerId)
{
	g_prev.erase(peerId);
	g_graphNotReadyLogged.erase(peerId);
}

void CMP_ResetAllPuppets()
{
	g_prev.clear();
	g_graphNotReadyLogged.clear();
	g_failedEvents.clear();
	g_failedVars.clear();
	g_loggedGraph = false;
}
