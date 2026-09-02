#include "pch.h"
#include "ghost.h"
#include "ghost/internal.h"
#include "appearance.h"
#include "crash.h"
#include "net.h"
#include "net/motion_interp.h"
#include "papyrus_util.h"
#include "puppet.h"
#include "session.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace cmp_ghost {

std::mutex g_cloneMutex;
std::unordered_map<std::uint32_t, RE::TESNPC*> g_peerBases;
std::unordered_set<std::uint32_t> g_cloneFormIds;
std::unordered_set<std::uint32_t> g_ghostReady;
std::unordered_map<std::uint32_t, int> g_freezeTicks;
std::unordered_map<std::uint32_t, double> g_lastMoveSec;
bool g_cloneSourceFailed = false;

RE::TESNPC* NpcFromForm(RE::TESForm* form)
{
	auto* npc = form ? form->As<RE::TESNPC>() : nullptr;
	if (!npc || cmp::forbidden_actor_base(npc->GetFormID())) {
		return nullptr;
	}
	return npc;
}

void SanitizeCloneFlags(RE::TESNPC* npc)
{
	if (!npc) {
		return;
	}
	npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kUnique);
	npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kIsChargenFacePreset);
	npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kUsesTemplate);
}

RE::TESNPC* FindCloneSource()
{
	if (g_cloneSourceFailed) {
		return nullptr;
	}
	const auto formId = CMP_Session().settings.ghostSourceForm;
	auto* npc = NpcFromForm(RE::TESForm::GetFormByID(formId));
	if (!npc) {
		g_cloneSourceFailed = true;
		REX::ERROR("Ghost clone source {:08X} missing or forbidden", formId);
		SetGhostNote("ghost SourceForm missing");
		return nullptr;
	}
	return npc;
}

RE::TESNPC* FinalizeClone(RE::TESNPC* npc, RE::TESNPC* source, std::uint32_t peerId, const char* path)
{
	if (!npc) {
		return nullptr;
	}
	npc->SetTemporary();
	SanitizeCloneFlags(npc);
	if (auto* data = RE::TESDataHandler::GetSingleton()) {
		if (!data->AddFormToDataHandler(npc)) {
			REX::WARN("AddFormToDataHandler failed for clone {:08X}", npc->GetFormID());
		}
	}
	npc->InitItemImpl();
	g_cloneFormIds.insert(npc->GetFormID());
	REX::INFO("CloneGhostBase peer={} form={:08X} via {} source={:08X} unique={} race={:08X}",
		peerId,
		npc->GetFormID(),
		path,
		source->GetFormID(),
		npc->IsUnique(),
		npc->GetFormRace() ? npc->GetFormRace()->GetFormID() : 0);
	return npc;
}

RE::TESNPC* CloneGhostBase(std::uint32_t peerId)
{
	std::lock_guard lock(g_cloneMutex);
	if (auto it = g_peerBases.find(peerId); it != g_peerBases.end() && it->second) {
		return it->second;
	}

	auto* source = FindCloneSource();
	if (!source) {
		return nullptr;
	}

	RE::TESNPC* npc = nullptr;
	if (auto* dup = source->CreateDuplicateForm(false, nullptr)) {
		npc = FinalizeClone(dup->As<RE::TESNPC>(), source, peerId, "CreateDuplicateForm");
		if (!npc) {
			REX::WARN("CreateDuplicateForm did not yield TESNPC for peer={}", peerId);
		}
	}

	if (!npc) {
		auto* factory = RE::ConcreteFormFactory<RE::TESNPC>::GetFormFactory();
		if (!factory) {
			REX::ERROR("ConcreteFormFactory<TESNPC> missing");
			SetGhostNote("TESNPC factory missing");
			return nullptr;
		}
		auto* created = factory->Create();
		if (!created) {
			REX::ERROR("TESNPC factory Create failed peer={}", peerId);
			SetGhostNote("TESNPC Create failed");
			return nullptr;
		}
		created->Copy(source);
		npc = FinalizeClone(created, source, peerId, "FactoryCreate+Copy");
	}

	if (!npc) {
		SetGhostNote("ghost clone failed");
		return nullptr;
	}
	g_peerBases[peerId] = npc;
	return npc;
}

void DropPeerClone(std::uint32_t peerId)
{
	std::lock_guard lock(g_cloneMutex);
	if (auto it = g_peerBases.find(peerId); it != g_peerBases.end()) {
		if (it->second) {
			g_cloneFormIds.erase(it->second->GetFormID());
		}
		g_peerBases.erase(it);
	}
	g_ghostReady.erase(peerId);
	g_freezeTicks.erase(peerId);
	g_lastMoveSec.erase(peerId);
	CMP_ResetGhostPuppet(peerId);
}

void ClearAllClones()
{
	std::lock_guard lock(g_cloneMutex);
	g_peerBases.clear();
	g_cloneFormIds.clear();
	g_ghostReady.clear();
	g_freezeTicks.clear();
	g_lastMoveSec.clear();
	g_cloneSourceFailed = false;
	CMP_ResetAllPuppets();
}

void EnsureGhost3D(RE::Actor* actor)
{
	if (!actor) {
		return;
	}
	if (auto* player = RE::PlayerCharacter::GetSingleton()) {
		if (auto* cell = player->GetParentCell()) {
			if (actor->GetParentCell() != cell) {
				actor->SetParentCell(cell);
			}
		}
	}
	if (actor->IsDisabled()) {
		actor->Enable(true);
	}
	actor->DoMoveToHigh();
	actor->Update3DPosition(true);
	actor->UpdateReference3D();
	if (auto* q = RE::TaskQueueInterface::GetSingleton()) {
		q->QueueUpdate3D(actor, 0);
	}
}

void FinishGhostSetup(RE::Actor* actor, const cmp::PlayerPose& pose, const char* path)
{
	if (!actor) {
		return;
	}
	REX::INFO("FinishGhostSetup begin {:08X} peer={} via {}", actor->GetFormID(), pose.peerId, path);
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player) {
		if (auto* cell = player->GetParentCell()) {
			actor->SetParentCell(cell);
		}
	}
	actor->Enable(true);
	actor->SetScale(1.0f);
	CMP_SetGhostLabel(actor, GhostLabel(pose).c_str());
	EnsureGhost3D(actor);
	if (!actor->Get3D()) {
		REX::WARN("Ghost {:08X} via {} has no 3D yet, will retry", actor->GetFormID(), path);
	} else {
		g_ghostReady.insert(pose.peerId);
		FreezeGhost(actor, pose.peerId);
		CMP_ApplyGhostAppearance(actor, pose.peerId);
		CMP_ApplyGhostInventory(actor, pose.peerId);
		CMP_ReapplyGhostPuppet(pose.peerId);
		REX::INFO("Ghost {:08X} 3D ready via {}", actor->GetFormID(), path);
	}
	SetGhostNote("");
	REX::INFO("Spawned ghost {:08X} for peer {} via {} at ({:.0f},{:.0f},{:.0f}) 3D={}",
		actor->GetFormID(), pose.peerId, path, pose.x, pose.y, pose.z, actor->Get3D() ? 1 : 0);
}

RE::Actor* SpawnGhostNative(const cmp::PlayerPose& pose)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* data = RE::TESDataHandler::GetSingleton();
	auto* base = CloneGhostBase(pose.peerId);
	if (!player || !data || !base) {
		return nullptr;
	}

	if (cmp::forbidden_actor_base(base->GetFormID())) {
		REX::ERROR("Refusing to place player/workshop ActorBase");
		SetGhostNote("refused forbidden ActorBase");
		return nullptr;
	}

	auto* cell = player->GetParentCell();
	if (!cell) {
		return nullptr;
	}

	const RE::NiPoint3 spawnAt{ pose.x, pose.y, pose.z };
	REX::INFO("CreateReferenceAtLocation begin base={:08X} peer={} cell={:08X} at ({:.0f},{:.0f},{:.0f})",
		base->GetFormID(),
		pose.peerId,
		cell->GetFormID(),
		spawnAt.x,
		spawnAt.y,
		spawnAt.z);
	RE::NEW_REFR_DATA spawn;
	spawn.location = spawnAt;
	spawn.direction = RE::NiPoint3{ 0.0f, 0.0f, pose.yaw };
	spawn.object = base;
	spawn.initializeScripts = false;
	spawn.forcePersist = false;
	spawn.clearStillLoadingFlag = true;
	spawn.initiallyDisabled = false;
	if (cell->IsInterior()) {
		spawn.interior = cell;
		spawn.world = nullptr;
	} else {
		spawn.interior = nullptr;
		spawn.world = cell->worldSpace;
	}

	const auto handle = data->CreateReferenceAtLocation(spawn);
	const auto ptr = handle.get();
	auto* actor = ptr ? ptr->As<RE::Actor>() : nullptr;
	if (!actor || !actor->IsActor()) {
		REX::WARN("CreateReferenceAtLocation did not yield an Actor (hasPtr={} type={})",
			ptr ? 1 : 0,
			ptr ? ptr->GetFormTypeString() : "null");
		SetGhostNote("CreateReferenceAtLocation failed");
		return nullptr;
	}
	if (actor->GetFormID() == base->GetFormID()) {
		REX::WARN("CreateReferenceAtLocation formId==base {:08X} type={} baseType={}",
			actor->GetFormID(),
			actor->GetFormTypeString(),
			base->GetFormTypeString());
	}
	REX::INFO("CreateReferenceAtLocation ok {:08X} base={:08X} type={}",
		actor->GetFormID(),
		base->GetFormID(),
		actor->GetFormTypeString());
	FinishGhostSetup(actor, pose, "CreateReferenceAtLocation");
	return actor;
}

RE::Actor* SpawnGhost(const cmp::PlayerPose& pose)
{
	return SpawnGhostNative(pose);
}

void MoveGhost(RE::Actor* actor, const cmp::PlayerPose& pose)
{
	if (!actor) {
		return;
	}
	if (!actor->Get3D()) {
		actor->SetPosition(RE::NiPoint3{ pose.x, pose.y, pose.z }, true);
		actor->SetHeading(pose.yaw);
		EnsureGhost3D(actor);
		return;
	}
	if (!g_ghostReady.contains(pose.peerId)) {
		g_ghostReady.insert(pose.peerId);
		FreezeGhost(actor, pose.peerId);
		CMP_ApplyGhostAppearance(actor, pose.peerId);
		CMP_ApplyGhostInventory(actor, pose.peerId);
		REX::INFO("Ghost {:08X} 3D attached, applied look", actor->GetFormID());
	} else {
		FreezeGhost(actor, pose.peerId);
	}

	using clock = std::chrono::steady_clock;
	const double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
	double dt = 1.0 / 20.0;
	if (auto it = g_lastMoveSec.find(pose.peerId); it != g_lastMoveSec.end()) {
		dt = std::clamp(now - it->second, 1.0 / 120.0, 0.25);
	}
	g_lastMoveSec[pose.peerId] = now;

	const float delayMs = CMP_EffectiveInterpDelayMs();
	cmp_motion::SampledTransform sampled;
	{
		auto& s = CMP_Session();
		std::lock_guard lock(s.mutex);
		auto rit = s.net.poseRing.find(pose.peerId);
		if (rit != s.net.poseRing.end() && !rit->second.samples.empty()) {
			const double renderSec = now - static_cast<double>(delayMs) / 1000.0;
			sampled = cmp_motion::SampleAt(rit->second, renderSec);
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
	std::uint32_t flags = sampled.flags;
	{
		auto& s = CMP_Session();
		std::lock_guard lock(s.mutex);
		if (s.ghosts.animOverridePeer == pose.peerId && now < s.ghosts.animOverrideUntil) {
			flags = s.ghosts.animOverrideFlags;
		}
	}
	CMP_ApplyGhostPuppet(actor, pose.peerId, sampled.pitch, sampled.yaw, sampled.speed, sampled.vx, sampled.vy, flags);
}

}  // namespace cmp_ghost

void CMP_DespawnGhosts()
{
	auto& s = CMP_Session();
	std::lock_guard lock(s.mutex);
	for (auto& [id, handle] : s.ghosts.byPeer) {
		if (const auto ptr = handle.get()) {
			ptr->Disable();
		}
	}
	s.ghosts.byPeer.clear();
	s.ghosts.names.clear();
	s.lastGhostNote.clear();
	s.ghosts.animOverridePeer = 0;
	s.ghosts.animOverrideUntil = 0;
	cmp_ghost::ClearAllClones();
}

void CMP_ApplyGhosts()
{
	CMP_CrashNote("ghosts");
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell()) {
		return;
	}

	const auto loc = cmp_ghost::PlayerLocationForm();
	auto& s = CMP_Session();

	std::unordered_map<std::uint32_t, cmp::PlayerPose> poses;
	{
		std::lock_guard lock(s.mutex);
		poses = s.net.latestPose;
	}

	std::unordered_set<std::uint32_t> live;
	if (!s.settings.ghostSpawn) {
		cmp_ghost::SetGhostNote("Ghost.Spawn=0 (join only, no bodies)");
	}

	for (const auto& [peer, pose] : poses) {
		if (peer == s.net.myPeerId) {
			continue;
		}
		if (!s.settings.ghostSpawn) {
			continue;
		}
		if (pose.locationFormId != 0 && loc != 0 && pose.locationFormId != loc) {
			cmp_ghost::SetGhostNote("remote in another cell (same-cell ghosts only)");
			continue;
		}
		live.insert(peer);

		RE::Actor* actor = nullptr;
		{
			std::lock_guard lock(s.mutex);
			auto it = s.ghosts.byPeer.find(peer);
			if (it != s.ghosts.byPeer.end()) {
				if (const auto ptr = it->second.get()) {
					actor = ptr->As<RE::Actor>();
				}
			}
		}
		if (!actor) {
			actor = cmp_ghost::SpawnGhost(pose);
			if (!actor) {
				continue;
			}
			std::lock_guard lock(s.mutex);
			s.ghosts.byPeer[peer] = actor->GetHandle();
		}
		cmp_ghost::MoveGhost(actor, pose);
	}

	std::lock_guard lock(s.mutex);
	for (auto it = s.ghosts.byPeer.begin(); it != s.ghosts.byPeer.end();) {
		if (!live.contains(it->first)) {
			if (const auto ptr = it->second.get()) {
				ptr->Disable();
			}
			cmp_ghost::DropPeerClone(it->first);
			it = s.ghosts.byPeer.erase(it);
		} else {
			++it;
		}
	}
}
