#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace RE {
class Actor;
class IAnimationGraphManagerHolder;
}

namespace cmp_puppet {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMoveSpeedEps = 5.0f;
constexpr float kSlowWalkMax = 120.0f;
constexpr int kStickyRetryTicks = 30;

struct PuppetPrev {
	std::uint32_t flags{ 0 };
	bool moving{ false };
	bool have{ false };
	int stickyTicks{ 0 };
	float yaw{ 0.0f };
};

extern std::unordered_map<std::uint32_t, PuppetPrev> g_prev;
extern std::unordered_set<std::uint32_t> g_graphNotReadyLogged;
extern std::unordered_set<std::string> g_failedEvents;
extern std::unordered_set<std::string> g_failedVars;
extern bool g_loggedGraph;

float WrapDeg(float d);
float WrapRad(float a);
void FireEvent(RE::IAnimationGraphManagerHolder* holder, const char* name);
void SetBoolVar(RE::Actor* actor, const char* name, bool value);
void SetIntVar(RE::Actor* actor, const char* name, int value);
void SetFloatVar(RE::Actor* actor, const char* name, float value);
bool GraphReady(RE::Actor* actor);
int WeaponTypeFor(RE::Actor* actor);
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
	bool inMenu);
void EdgeFlag(
	RE::Actor* actor,
	bool had,
	bool now,
	const char* onEvent,
	const char* offEvent,
	const char* onAlt = nullptr,
	const char* offAlt = nullptr);
void ApplyCombatFlags(
	RE::Actor* actor,
	std::uint32_t prevFlags,
	std::uint32_t flags,
	bool hadPrev,
	bool isGhost);

}  // namespace cmp_puppet
