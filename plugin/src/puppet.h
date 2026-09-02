#pragma once

#include "cmp_protocol.hpp"

#include <cstdint>

namespace RE
{
	class Actor;
}

void CMP_FillLocalMotion(cmp::PlayerPose& pose);
void CMP_FillActorMotion(RE::Actor* actor, float& pitch, float& speed, float& vx, float& vy, std::uint32_t& flags);
void CMP_ApplyGhostPuppet(RE::Actor* actor, const cmp::PlayerPose& pose);
void CMP_ApplyGhostPuppet(RE::Actor* actor, std::uint32_t id, float pitch, float yaw, float speed, float vx, float vy, std::uint32_t flags);
void CMP_ResetGhostPuppet(std::uint32_t peerId);
void CMP_ResetAllPuppets();
float CMP_InterpolateHeading(float currentRad, float targetRad, float alpha);
