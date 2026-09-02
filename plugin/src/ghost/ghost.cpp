#include "pch.h"
#include "ghost.h"
#include "ghost/internal.h"
#include "papyrus_util.h"
#include "puppet.h"
#include "session.h"

#include <chrono>
#include <mutex>
#include <string>

namespace cmp_ghost {

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

void SetGhostNote(std::string note)
{
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	s.lastGhostNote = std::move(note);
}

std::string GhostLabel(const cmp::PlayerPose& pose)
{
	std::string label = "Player";
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	if (cmp::is_fake_peer(pose.peerId) || pose.peerId == s.net.fakePeerId) {
		label = "Dummy";
	} else if (auto it = s.ghosts.names.find(pose.peerId); it != s.ghosts.names.end() && !it->second.empty()) {
		label = it->second;
	}
	return label;
}

void FreezeGhost(RE::Actor* actor, std::uint32_t peerId)
{
	if (!actor) {
		return;
	}

	// Do not set kMovementBlocked: it freezes locomotion while we SetPosition the root.
	// Do not set kAttackingDisabled: it blocks weaponDraw / jump graph edges on puppets.
	actor->boolFlags.set(
		RE::Actor::BOOL_FLAGS::kCastingDisabled,
		RE::Actor::BOOL_FLAGS::kDoNotShowOnStealthMeter,
		RE::Actor::BOOL_FLAGS::kShouldAnimGraphUpdate);
	actor->boolFlags.reset(RE::Actor::BOOL_FLAGS::kAttackOnSight);
	actor->boolFlags.reset(RE::Actor::BOOL_FLAGS::kMovementBlocked);
	actor->boolFlags.reset(RE::Actor::BOOL_FLAGS::kAttackingDisabled);

	int& tick = g_freezeTicks[peerId];
	++tick;
	actor->StopCombat();
	// EnableAI(false) once on first freeze / respawn only. Re-calling it every
	// ~30 ticks snaps locomotion back to standing idle.
	if (tick == 1) {
		actor->SetLifeState(RE::ACTOR_LIFE_STATE::kAlive);
		CMP_CallActorPapyrus(actor, "EnableAI", false);
	}
}

}  // namespace cmp_ghost

void CMP_FreezeRemoteActor(RE::Actor* actor, std::uint32_t id)
{
	cmp_ghost::FreezeGhost(actor, id);
}

void CMP_UnfreezeRemoteActor(RE::Actor* actor, std::uint32_t id)
{
	if (!actor) {
		return;
	}
	actor->boolFlags.reset(RE::Actor::BOOL_FLAGS::kCastingDisabled);
	actor->boolFlags.reset(RE::Actor::BOOL_FLAGS::kDoNotShowOnStealthMeter);
	actor->boolFlags.reset(RE::Actor::BOOL_FLAGS::kAttackingDisabled);
	CMP_CallActorPapyrus(actor, "EnableAI", true);
	CMP_ResetGhostPuppet(id);
}

void CMP_ReapplyGhostPuppet(std::uint32_t peerId)
{
	auto& s = CMP_Session();
	cmp::PlayerPose pose{};
	RE::Actor* actor = nullptr;
	{
		std::lock_guard lock(s.mutex);
		auto pit = s.net.latestPose.find(peerId);
		if (pit == s.net.latestPose.end()) {
			return;
		}
		pose = pit->second;
		auto git = s.ghosts.byPeer.find(peerId);
		if (git == s.ghosts.byPeer.end()) {
			return;
		}
		if (const auto ptr = git->second.get()) {
			actor = ptr->As<RE::Actor>();
		}
	}
	if (actor) {
		CMP_ResetGhostPuppet(peerId);
		CMP_ApplyGhostPuppet(actor, pose);
	}
}

bool CMP_IsPaintableGhostBase(RE::TESNPC* npc)
{
	if (!npc || cmp::forbidden_actor_base(npc->GetFormID())) {
		return false;
	}
	std::lock_guard lock(cmp_ghost::g_cloneMutex);
	return cmp_ghost::g_cloneFormIds.contains(npc->GetFormID());
}

bool CMP_ForceAnim(int step, std::string& note)
{
	if (step < 0) {
		step = 0;
	}
	step %= cmp::kFakeAnimStepCount;
	const auto flags = cmp::fake_anim_flags(step * cmp::kFakeAnimStepTicks, 0);

	RE::Actor* actor = nullptr;
	std::uint32_t peer = 0;
	auto& s = CMP_Session();
	{
		std::lock_guard lock(s.mutex);
		for (const auto& [id, handle] : s.ghosts.byPeer) {
			const auto ptr = handle.get();
			auto* candidate = ptr ? ptr->As<RE::Actor>() : nullptr;
			if (!candidate || !candidate->Get3D()) {
				continue;
			}
			if (peer == 0 || cmp::is_fake_peer(id)) {
				actor = candidate;
				peer = id;
				if (cmp::is_fake_peer(id)) {
					break;
				}
			}
		}
	}

	if (!actor) {
		actor = RE::PlayerCharacter::GetSingleton();
		if (!actor) {
			note = "cmp_anim: no actor";
			return false;
		}
		peer = 0xFFFFFFFFu;
		note = std::string("cmp_anim: no dummy (join with fake on), applying to you ") + cmp::fake_anim_name(flags);
	} else {
		using clock = std::chrono::steady_clock;
		std::lock_guard lock(s.mutex);
		s.ghosts.animOverridePeer = peer;
		s.ghosts.animOverrideFlags = flags;
		s.ghosts.animOverrideUntil = std::chrono::duration<double>(clock::now().time_since_epoch()).count() + 4.0;
		note = std::string("cmp_anim: peer ") + std::to_string(peer) + " " + cmp::fake_anim_name(flags) + " (4s)";
	}

	float pitch = 0.0f;
	float speed = 0.0f;
	float vx = 0.0f;
	float vy = 0.0f;
	std::uint32_t ignored = 0;
	CMP_FillActorMotion(actor, pitch, speed, vx, vy, ignored);
	CMP_ApplyGhostPuppet(actor, peer, pitch, actor->GetHeading(), speed, vx, vy, flags);
	return true;
}

int CMP_CountGhostsWith3D()
{
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	int n = 0;
	for (const auto& [id, handle] : s.ghosts.byPeer) {
		if (const auto ptr = handle.get()) {
			if (auto* actor = ptr->As<RE::Actor>(); actor && actor->Get3D()) {
				++n;
			}
		}
	}
	return n;
}

std::uint32_t CMP_PeerForGhost(RE::Actor* actor)
{
	if (!actor) {
		return 0;
	}
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	for (const auto& [peer, handle] : s.ghosts.byPeer) {
		const auto ptr = handle.get();
		if (ptr && ptr->As<RE::Actor>() == actor) {
			return peer;
		}
	}
	return 0;
}
