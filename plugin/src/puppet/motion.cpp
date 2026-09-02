#include "pch.h"
#include "puppet.h"
#include "puppet/internal.h"
#include "ui_pose.h"

#include <cmath>

using namespace cmp_puppet;

void CMP_FillActorMotion(RE::Actor* actor, float& pitch, float& speed, float& vx, float& vy, std::uint32_t& flags)
{
	if (!actor) {
		return;
	}

	RE::NiPoint3 angles{};
	actor->DoGetEulerAngles(angles);
	pitch = angles.x;
	speed = actor->DoGetCurrentSpeed();

	RE::NiPoint3 vel{};
	actor->GetLinearVelocity(vel);
	vx = vel.x;
	vy = vel.y;
	if (speed <= 0.01f) {
		speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
	}

	flags = CMP_LocalMenuFlags();
	if (actor->IsSneaking()) {
		flags |= cmp::PoseFlag::Sneak;
	}
	if (actor->DoGetSprinting()) {
		flags |= cmp::PoseFlag::Sprint;
	} else if (speed > kMoveSpeedEps && speed < kSlowWalkMax) {
		flags |= cmp::PoseFlag::SlowWalk;
	}
	if (actor->GetWeaponMagicDrawn()) {
		flags |= cmp::PoseFlag::Drawn;
	}

	const auto gun = actor->gunState;
	if (gun == RE::GUN_STATE::kReloading) {
		flags |= cmp::PoseFlag::Reloading;
	}
	if (gun == RE::GUN_STATE::kFire || gun == RE::GUN_STATE::kFireSighted) {
		flags |= cmp::PoseFlag::Attacking;
	}
	if (gun == RE::GUN_STATE::kSighted || gun == RE::GUN_STATE::kFireSighted) {
		flags |= cmp::PoseFlag::Sighted;
	}
	if (actor->meleeAttackState != 0) {
		flags |= cmp::PoseFlag::Attacking;
	}

	const auto body = actor->DoGetCharacterState();
	if (body == RE::IMovementState::CHARACTER_STATE::kJumping
		|| body == RE::IMovementState::CHARACTER_STATE::kInAir) {
		flags |= cmp::PoseFlag::Jumping;
	}
	if (actor->IsDead(true)) {
		flags |= cmp::PoseFlag::Dead;
	}
}

void CMP_FillLocalMotion(cmp::PlayerPose& pose)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}
	CMP_FillActorMotion(player, pose.pitch, pose.speed, pose.vx, pose.vy, pose.flags);
}

float CMP_InterpolateHeading(float currentRad, float targetRad, float alpha)
{
	const float delta = WrapRad(targetRad - currentRad);
	return currentRad + delta * std::clamp(alpha, 0.0f, 1.0f);
}
