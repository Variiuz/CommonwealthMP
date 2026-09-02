#include "pch.h"
#include "session.h"
#include "actors.h"
#include "crash.h"
#include "ghost.h"
#include "net.h"
#include "puppet.h"
#include "udp_win.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr int kMaxHostActors = 24;

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

std::uint32_t ActorLocationForm(RE::Actor* actor)
{
	if (!actor) {
		return 0;
	}
	auto* cell = actor->GetParentCell();
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

bool IsGhostCloneActor(RE::Actor* actor)
{
	if (!actor) {
		return false;
	}
	return CMP_IsPaintableGhostBase(actor->GetNPC());
}

bool ShouldPublish(RE::Actor* actor, std::uint32_t hostLoc)
{
	if (!actor || actor->IsPlayerRef() || actor->IsDead(true) || actor->IsDisabled()) {
		return false;
	}
	if (actor == RE::PlayerCharacter::GetSingleton()) {
		return false;
	}
	if (IsGhostCloneActor(actor)) {
		return false;
	}
	auto* npc = actor->GetNPC();
	if (!npc || !npc->IsUnique() || cmp::forbidden_actor_base(npc->GetFormID())) {
		return false;
	}
	const auto loc = ActorLocationForm(actor);
	if (hostLoc != 0 && loc != 0 && loc != hostLoc) {
		return false;
	}
	return true;
}

void CollectHandles(std::vector<RE::Actor*>& out, const RE::BSTArray<RE::ActorHandle>& handles, std::uint32_t hostLoc)
{
	for (const auto& handle : handles) {
		auto ptr = handle.get();
		if (!ptr) {
			continue;
		}
		auto* actor = ptr.get();
		if (!ShouldPublish(actor, hostLoc)) {
			continue;
		}
		if (std::find(out.begin(), out.end(), actor) != out.end()) {
			continue;
		}
		out.push_back(actor);
		if (static_cast<int>(out.size()) >= kMaxHostActors) {
			return;
		}
	}
}

void MoveUnique(RE::Actor* actor, const cmp::ActorPose& pose)
{
	if (!actor) {
		return;
	}

	using clock = std::chrono::steady_clock;
	static std::unordered_map<std::uint32_t, double> lastMove;
	const double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	double dt = 1.0 / 20.0;
	if (auto it = lastMove.find(pose.refFormId); it != lastMove.end()) {
		dt = std::clamp(now - it->second, 1.0 / 120.0, 0.25);
	}
	lastMove[pose.refFormId] = now;

	const float delayMs = CMP_EffectiveInterpDelayMs();
	cmp_motion::SampledTransform sampled;
	{
		auto& s = CMP_Session();
		std::lock_guard lock(s.mutex);
		auto rit = s.net.actorRing.find(pose.refFormId);
		if (rit != s.net.actorRing.end() && !rit->second.samples.empty()) {
			sampled = cmp_motion::SampleAt(rit->second, now - static_cast<double>(delayMs) / 1000.0);
		}
	}
	if (!sampled.valid) {
		sampled.x = pose.x;
		sampled.y = pose.y;
		sampled.z = pose.z;
		sampled.yaw = pose.yaw;
		sampled.pitch = pose.pitch;
		sampled.speed = pose.speed;
		sampled.vx = pose.vx;
		sampled.vy = pose.vy;
		sampled.flags = pose.flags;
		sampled.valid = true;
	}

	const auto cur = actor->GetPosition();
	const auto stepped = cmp_motion::StepToward(
		cur.x, cur.y, cur.z, actor->GetHeading(), sampled, static_cast<float>(dt), delayMs);
	actor->SetPosition(RE::NiPoint3{ stepped.x, stepped.y, stepped.z }, true);
	const float heading = CMP_InterpolateHeading(
		actor->GetHeading(), sampled.yaw, std::clamp(static_cast<float>(dt) * 12.0f, 0.1f, 1.0f));
	actor->SetHeading(heading);
	CMP_FreezeRemoteActor(actor, pose.refFormId);
	CMP_ApplyGhostPuppet(actor, pose.refFormId, sampled.pitch, sampled.yaw, sampled.speed, sampled.vx, sampled.vy, sampled.flags);
}

std::unordered_set<std::uint32_t> g_puppeted;
std::unordered_map<std::uint32_t, double> g_actorSeen;

}  // namespace

void CMP_OnActorPose(const cmp::ActorPose& pose)
{
	if (!pose.refFormId) {
		return;
	}
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	if (!s.net.joined || s.net.isHost) {
		return;
	}
	s.net.latestActors[pose.refFormId] = pose;
	using clock = std::chrono::steady_clock;
	const double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	g_actorSeen[pose.refFormId] = now;
	s.net.actorRing[pose.refFormId].push(cmp_motion::FromActorPose(pose, now));
}

void CMP_SendHostActors()
{
	auto& s = CMP_Session();
	if (!s.net.joined || !s.net.isHost) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* lists = RE::ProcessLists::GetSingleton();
	if (!player || !lists) {
		return;
	}

	const auto hostLoc = PlayerLocationForm();
	std::vector<RE::Actor*> actors;
	actors.reserve(static_cast<std::size_t>(kMaxHostActors));
	CollectHandles(actors, lists->highActorHandles, hostLoc);
	if (static_cast<int>(actors.size()) < kMaxHostActors) {
		CollectHandles(actors, lists->middleHighActorHandles, hostLoc);
	}

	for (auto* actor : actors) {
		const auto pos = actor->GetPosition();
		auto* npc = actor->GetNPC();
		float pitch = 0.0f;
		float speed = 0.0f;
		float vx = 0.0f;
		float vy = 0.0f;
		std::uint32_t flags = 0;
		CMP_FillActorMotion(actor, pitch, speed, vx, vy, flags);
		const auto msg = cmp::make_actor_pose(
			actor->GetFormID(),
			npc ? npc->GetFormID() : 0,
			ActorLocationForm(actor),
			pos.x,
			pos.y,
			pos.z,
			actor->GetHeading(),
			pitch,
			speed,
			vx,
			vy,
			flags,
			true,
			false);
		cmp_udp_send(s.settings.host.c_str(), s.settings.port, &msg, static_cast<int>(sizeof(msg)));
	}
}

void CMP_ApplyHostActors()
{
	CMP_CrashNote("actors");
	auto& s = CMP_Session();
	if (!s.net.joined || s.net.isHost) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell()) {
		return;
	}

	const auto loc = PlayerLocationForm();
	std::unordered_map<std::uint32_t, cmp::ActorPose> poses;
	std::unordered_map<std::uint32_t, double> seen;
	{
		std::lock_guard lock(s.mutex);
		poses = s.net.latestActors;
		seen = g_actorSeen;
	}

	std::unordered_set<std::uint32_t> live;
	using clock = std::chrono::steady_clock;
	const double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	for (const auto& [id, pose] : poses) {
		if (auto itSeen = seen.find(id); itSeen == seen.end() || now - itSeen->second > 1.5) {
			continue;
		}
		if (pose.locationFormId != 0 && loc != 0 && pose.locationFormId != loc) {
			continue;
		}
		if (!pose.unique || pose.dead) {
			continue;
		}
		auto* form = RE::TESForm::GetFormByID(pose.refFormId);
		auto* actor = form ? form->As<RE::Actor>() : nullptr;
		if (!actor || actor->IsPlayerRef() || IsGhostCloneActor(actor)) {
			continue;
		}
		live.insert(pose.refFormId);
		g_puppeted.insert(pose.refFormId);
		MoveUnique(actor, pose);
	}

	for (auto it = g_puppeted.begin(); it != g_puppeted.end();) {
		if (live.contains(*it)) {
			++it;
			continue;
		}
		if (auto* form = RE::TESForm::GetFormByID(*it)) {
			if (auto* actor = form->As<RE::Actor>()) {
				CMP_UnfreezeRemoteActor(actor, *it);
			}
		}
		it = g_puppeted.erase(it);
	}
}

void CMP_ClearHostActors()
{
	for (const auto id : g_puppeted) {
		if (auto* form = RE::TESForm::GetFormByID(id)) {
			if (auto* actor = form->As<RE::Actor>()) {
				CMP_UnfreezeRemoteActor(actor, id);
			}
		}
	}
	g_puppeted.clear();
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	g_actorSeen.clear();
	s.net.latestActors.clear();
}
