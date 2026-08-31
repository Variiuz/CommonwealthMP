#include "pch.h"
#include "cmp.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMoveSpeedEps = 20.0f;
constexpr int kMoveStartRefreshTicks = 40; // ~2s at 20 Hz

struct PuppetPrev {
	std::uint32_t flags{ 0 };
	bool moving{ false };
	bool have{ false };
	int movingTicks{ 0 };
};

std::unordered_map<std::uint32_t, PuppetPrev> g_prev;
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

void SetDumpVars(RE::Actor* actor, const cmp::PlayerPose& pose, bool moving)
{
	SetFloatVar(actor, "Speed", pose.speed);
	SetFloatVar(actor, "SpeedSampled", pose.speed);
	float dir = 0.0f;
	if (moving) {
		const float moveYaw = std::atan2(pose.vx, pose.vy);
		dir = WrapDeg((moveYaw - pose.yaw) * (180.0f / kPi));
	}
	SetFloatVar(actor, "Direction", dir);
	SetBoolVar(actor, "IsSneaking", cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Sneak));
	SetBoolVar(actor, "IsSprinting", cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Sprint));
	SetBoolVar(actor, "bInJumpState", cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Jumping));
	SetBoolVar(actor, "bWantGait", moving);
	SetIntVar(actor, "iWantGait", moving ? 1 : 0);
	SetIntVar(actor, "iSyncIdleLocomotion", moving ? 1 : 0);
}

}  // namespace

void CMP_FillLocalMotion(cmp::PlayerPose& pose)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	RE::NiPoint3 angles{};
	player->DoGetEulerAngles(angles);
	pose.pitch = angles.x;
	pose.speed = player->DoGetCurrentSpeed();

	RE::NiPoint3 vel{};
	player->GetLinearVelocity(vel);
	pose.vx = vel.x;
	pose.vy = vel.y;
	if (pose.speed <= 0.01f) {
		pose.speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
	}

	std::uint32_t flags = 0;
	if (player->IsSneaking()) {
		flags |= cmp::PoseFlag::Sneak;
	}
	if (player->DoGetSprinting()) {
		flags |= cmp::PoseFlag::Sprint;
	}
	if (player->GetWeaponMagicDrawn()) {
		flags |= cmp::PoseFlag::Drawn;
	}

	const auto gun = player->gunState;
	if (gun == RE::GUN_STATE::kReloading) {
		flags |= cmp::PoseFlag::Reloading;
	}
	if (gun == RE::GUN_STATE::kFire || gun == RE::GUN_STATE::kFireSighted) {
		flags |= cmp::PoseFlag::Attacking;
	}
	if (gun == RE::GUN_STATE::kSighted || gun == RE::GUN_STATE::kFireSighted) {
		flags |= cmp::PoseFlag::Sighted;
	}
	if (player->meleeAttackState != 0) {
		flags |= cmp::PoseFlag::Attacking;
	}

	const auto body = player->DoGetCharacterState();
	if (body == RE::IMovementState::CHARACTER_STATE::kJumping
		|| body == RE::IMovementState::CHARACTER_STATE::kInAir) {
		flags |= cmp::PoseFlag::Jumping;
	}
	pose.flags = flags;
}

void CMP_ApplyGhostPuppet(RE::Actor* actor, const cmp::PlayerPose& pose)
{
	CMP_CrashNote("puppet");
	if (!GraphReady(actor)) {
		return;
	}

	actor->data.angle.x = pose.pitch;
	const bool moving = pose.speed > kMoveSpeedEps;
	SetDumpVars(actor, pose, moving);

	auto& prev = g_prev[pose.peerId];
	if (!prev.have || prev.moving != moving) {
		FireEvent(actor, moving ? "MoveStart" : "MoveStop");
		prev.movingTicks = 0;
	} else if (moving) {
		++prev.movingTicks;
		if (prev.movingTicks >= kMoveStartRefreshTicks) {
			FireEvent(actor, "MoveStart");
			prev.movingTicks = 0;
		}
	}

	if (prev.have) {
		const bool wasSneak = cmp::has_pose_flag(prev.flags, cmp::PoseFlag::Sneak);
		const bool nowSneak = cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Sneak);
		if (wasSneak != nowSneak) {
			FireEvent(actor, nowSneak ? "sneakStart" : "sneakStop");
		}
		const bool wasSprint = cmp::has_pose_flag(prev.flags, cmp::PoseFlag::Sprint);
		const bool nowSprint = cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Sprint);
		if (wasSprint != nowSprint) {
			FireEvent(actor, nowSprint ? "sprintStart" : "sprintStop");
		}
	}

	prev.flags = pose.flags;
	prev.moving = moving;
	prev.have = true;

	if (!g_loggedGraph) {
		g_loggedGraph = true;
		REX::INFO("Puppet first apply peer={} spd={:.1f} flags={:X} drawn={}",
			pose.peerId, pose.speed, pose.flags,
			cmp::has_pose_flag(pose.flags, cmp::PoseFlag::Drawn) ? 1 : 0);
	}
}

void CMP_ResetGhostPuppet(std::uint32_t peerId)
{
	g_prev.erase(peerId);
}

void CMP_ResetAllPuppets()
{
	g_prev.clear();
	g_failedEvents.clear();
	g_failedVars.clear();
	g_loggedGraph = false;
}
